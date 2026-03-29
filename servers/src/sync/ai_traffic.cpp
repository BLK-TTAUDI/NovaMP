// servers/src/sync/ai_traffic.cpp
#include "ai_traffic.hpp"
#include "../config/config.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

namespace novaMP {

// ── Tuning constants ──────────────────────────────────────────────────────────
static constexpr float PI    = 3.14159265358979323846f;
static constexpr float TWO_PI= 2.0f * PI;

// Vehicle model
static constexpr float WHEELBASE      = 2.7f;    // metres
static constexpr float MAX_STEER_RAD  = 0.6109f; // 35°
static constexpr float ACCEL_MPS2     = 4.0f;
static constexpr float BRAKE_MPS2     = 10.0f;   // harder brake for avoidance
static constexpr float WAYPOINT_REACH = 5.0f;    // metres to advance node
static constexpr float STEER_GAIN     = 2.5f;

// Approach braking
static constexpr float BRAKING_DIST   = 20.0f;

// Player detection
static constexpr float DETECT_DIST        = 40.0f;  // forward scan range
static constexpr float SLOW_DIST          = 30.0f;  // start slowing
static constexpr float STOP_DIST          = 8.0f;   // full stop
static constexpr float AVOID_DIST         = 15.0f;  // apply lateral offset
static constexpr float DETECT_HALF_COS   = 0.5f;   // cos(60°) — forward arc half-angle
static constexpr float LANE_HALF_WIDTH    = 2.0f;   // metres — in-lane threshold

// Rerouting
static constexpr float REROUTE_TIMEOUT   = 8.0f;   // seconds before new path

// Indicators
static constexpr float INDICATOR_DIST      = 28.0f; // metres before turn
static constexpr float INDICATOR_TURN_RAD  = 0.35f; // ~20° turn threshold
static constexpr float INDICATOR_BLINK_HZ  = 1.5f;  // blinks/second

// Intersection
static constexpr float INTERSECTION_SLOW   = 0.60f; // speed multiplier
static constexpr float INTERSECTION_STOP_D = 4.0f;  // hold before entering

// ── Utility ───────────────────────────────────────────────────────────────────
static float dist2(float ax, float ay, float bx, float by) {
    float dx=ax-bx, dy=ay-by; return std::sqrt(dx*dx+dy*dy);
}
static float dist3(float ax,float ay,float az,float bx,float by,float bz){
    float dx=ax-bx,dy=ay-by,dz=az-bz; return std::sqrt(dx*dx+dy*dy+dz*dz);
}
static float wrap(float a) {
    while (a >  PI) a -= TWO_PI;
    while (a < -PI) a += TWO_PI;
    return a;
}
static float clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ── Constructor ───────────────────────────────────────────────────────────────
AITraffic::AITraffic(const ServerConfig& cfg, BroadcastFn broadcast)
    : m_cfg(cfg), m_broadcast(std::move(broadcast))
    , m_speed_limit(cfg.ai_speed_limit), m_mode(cfg.ai_mode)
{
    m_vehicles.resize(55);
    for (int i = 0; i < 55; ++i) {
        m_vehicles[i].vehicle_id = (uint8_t)(200 + i);
        m_vehicles[i].active     = false;
    }
}

void AITraffic::loadRoadNetwork(const std::string& map_name) {
    m_road_net.load("Resources/Server/maps/" + map_name);
    spdlog::info("AI: Road network — {} nodes, map='{}'",
                 m_road_net.nodeCount(), map_name);
}

// ── Main tick ─────────────────────────────────────────────────────────────────
void AITraffic::tick(float dt) {
    if (m_mode == "parked") return;

    int active = activeCount();
    for (auto& v : m_vehicles) {
        if (!v.active && active < m_cfg.ai_count) {
            auto pos       = spawnPositionNearPlayers();
            v.model        = randomModel();
            v.state        = {};
            v.state.pos[0] = pos[0];
            v.state.pos[1] = pos[1];
            v.state.pos[2] = pos[2];
            v.state.rot[3] = 1.0f;
            v.state.vflags = VF_IS_AI;
            v.heading      = ((float)(nextRng() % 10000) / 10000.0f) * TWO_PI;
            v.speed        = 0.0f;
            v.speed_target = m_speed_limit *
                (0.75f + 0.25f * ((float)(nextRng() % 1000) / 1000.0f));
            v.ai_state      = AIState::DRIVING;
            v.state_timer   = 0.0f;
            v.blocked_timer = 0.0f;
            v.indicator     = 0;
            v.lateral_offset= 0.0f;
            v.active        = true;
            planPath(v);
            ++active;
        }
    }

    for (auto& v : m_vehicles) {
        if (!v.active) continue;
        if (shouldDespawn(v)) { v.active = false; continue; }
        tickVehicle(v, dt);
    }
}

// ── Per-vehicle update ────────────────────────────────────────────────────────
void AITraffic::tickVehicle(AIVehicle& v, float dt) {

    // ── Replan timer ──────────────────────────────────────────────────────────
    v.replan_timer -= dt;
    if (v.replan_timer <= 0.0f || v.path.empty())
        planPath(v);

    if (v.path.empty() || v.path_step >= (int)v.path.size()) {
        v.replan_timer = 0.0f;
        return;
    }

    // ── Forward arc obstacle scan ─────────────────────────────────────────────
    ObstacleInfo obs = scanForwardArc(v);

    // ── State machine ─────────────────────────────────────────────────────────
    v.state_timer += dt;

    switch (v.ai_state) {

    case AIState::DRIVING:
        if (obs.detected && obs.distance < SLOW_DIST) {
            v.ai_state    = AIState::SLOWING;
            v.state_timer = 0.0f;
        }
        break;

    case AIState::SLOWING:
        if (!obs.detected || obs.distance >= SLOW_DIST) {
            // Obstacle gone — return to normal
            v.ai_state      = AIState::DRIVING;
            v.state_timer   = 0.0f;
            v.blocked_timer = 0.0f;
            v.lateral_offset= 0.0f;
        } else if (obs.distance < STOP_DIST) {
            v.ai_state    = AIState::STOPPED;
            v.state_timer = 0.0f;
        } else {
            v.blocked_timer += dt;
        }
        break;

    case AIState::STOPPED:
        if (!obs.detected || obs.distance >= SLOW_DIST) {
            v.ai_state      = AIState::DRIVING;
            v.state_timer   = 0.0f;
            v.blocked_timer = 0.0f;
            v.lateral_offset= 0.0f;
        } else {
            v.blocked_timer += dt;
            // Been blocked a long time — find a way around
            if (v.blocked_timer > REROUTE_TIMEOUT) {
                v.ai_state    = AIState::REROUTING;
                v.state_timer = 0.0f;
            }
        }
        break;

    case AIState::REROUTING:
        // Force replan to a different goal on the next planPath call
        planPath(v);
        v.ai_state      = AIState::DRIVING;
        v.state_timer   = 0.0f;
        v.blocked_timer = 0.0f;
        v.lateral_offset= 0.0f;
        break;
    }

    // ── Lateral avoidance offset ──────────────────────────────────────────────
    // When slowing due to an obstacle, shift to the opposite side to go around.
    float target_lateral = 0.0f;
    if ((v.ai_state == AIState::SLOWING || v.ai_state == AIState::STOPPED)
        && obs.detected && obs.distance < AVOID_DIST)
    {
        // Obstacle is to the right → we shift left, and vice-versa.
        // Shift by just under one lane width so we stay on the road.
        float lane_w = (v.path_step < (int)v.path.size())
            ? m_road_net.node(v.path[v.path_step]).lane_width
            : 3.5f;
        target_lateral = (obs.lateral >= 0.0f ? -1.0f : 1.0f) * lane_w * 0.55f;
    }
    // Smooth transition toward target offset
    float delta = target_lateral - v.lateral_offset;
    v.lateral_offset += clamp(delta, -3.0f * dt, 3.0f * dt);

    // ── Target waypoint with lateral offset applied ───────────────────────────
    const RoadNode& tgt_node = m_road_net.node(v.path[v.path_step]);
    // Perpendicular right of AI heading
    float perp_x = std::sin(v.heading);
    float perp_y = -std::cos(v.heading);

    float tgt_x = tgt_node.x + perp_x * v.lateral_offset;
    float tgt_y = tgt_node.y + perp_y * v.lateral_offset;
    float tgt_z = tgt_node.z;

    float dx   = tgt_x - v.state.pos[0];
    float dy   = tgt_y - v.state.pos[1];
    float dist = dist2(v.state.pos[0], v.state.pos[1], tgt_x, tgt_y);

    // Advance to next node
    if (dist < WAYPOINT_REACH) {
        v.path_step++;
        if (v.path_step >= (int)v.path.size()) planPath(v);
        return;
    }

    // ── Intersection check ────────────────────────────────────────────────────
    bool at_intersection = intersectionAhead(v);
    bool intersection_occupied = at_intersection &&
                                 intersectionOccupied(v.path[v.path_step], v);

    // ── Speed computation ─────────────────────────────────────────────────────
    float node_limit = (m_mode == "random") ? m_speed_limit
                                            : std::min(tgt_node.speed_limit, m_speed_limit);

    // Apply intersection slow-down
    if (at_intersection) node_limit *= INTERSECTION_SLOW;

    // Apply obstacle state machine reduction
    float obstacle_factor = 1.0f;
    if (v.ai_state == AIState::SLOWING && obs.detected) {
        obstacle_factor = clamp((obs.distance - STOP_DIST) / (SLOW_DIST - STOP_DIST),
                                0.1f, 1.0f);
    } else if (v.ai_state == AIState::STOPPED || intersection_occupied) {
        obstacle_factor = 0.0f;
    }

    float max_speed = node_limit * obstacle_factor;

    // Approach braking near waypoint
    float approach = (dist < BRAKING_DIST)
        ? max_speed * (dist / BRAKING_DIST) : max_speed;
    approach = std::max(approach, (obstacle_factor > 0.01f) ? 0.5f : 0.0f);

    // Smooth accel / hard brake
    float dv = approach - v.speed;
    float rate = (dv < 0) ? -BRAKE_MPS2 : ACCEL_MPS2;
    v.speed += clamp(dv, rate * dt, ACCEL_MPS2 * dt);
    v.speed  = std::max(v.speed, 0.0f);

    // ── Ackermann steering ────────────────────────────────────────────────────
    float bearing     = std::atan2(dy, dx);
    float heading_err = wrap(bearing - v.heading);
    float steer       = clamp(heading_err * STEER_GAIN, -MAX_STEER_RAD, MAX_STEER_RAD);

    float omega = 0.0f;
    if (std::fabs(steer) > 0.001f)
        omega = v.speed / (WHEELBASE / std::tan(steer));
    v.heading += omega * dt;

    // ── Position update ───────────────────────────────────────────────────────
    v.state.pos[0] += v.speed * std::cos(v.heading) * dt;
    v.state.pos[1] += v.speed * std::sin(v.heading) * dt;
    float dz = tgt_z - v.state.pos[2];
    v.state.pos[2] += clamp(dz, -2.0f * dt, 2.0f * dt);

    // Velocity for client-side interpolation
    v.state.vel[0] = v.speed * std::cos(v.heading);
    v.state.vel[1] = v.speed * std::sin(v.heading);
    v.state.vel[2] = dz * 0.5f;

    // Quaternion from heading (yaw-only)
    float hy = v.heading * 0.5f;
    v.state.rot[0] = 0.0f;
    v.state.rot[1] = 0.0f;
    v.state.rot[2] = std::sin(hy);
    v.state.rot[3] = std::cos(hy);

    // ── Gauge simulation ──────────────────────────────────────────────────────
    static const float GT[] = {0,4,10,18,28,40,55};
    int gear = 1;
    for (int g = 1; g <= 6 && v.speed > GT[g]; ++g) gear = g;
    v.state.gear = (int8_t)gear;
    float gf = (v.speed - GT[gear-1]) / std::max(1.0f, GT[gear] - GT[gear-1]);
    v.state.rpm    = 900.0f + clamp(gf, 0.0f, 1.0f) * 5500.0f;
    v.state.health = 65535;
    v.state.vflags = VF_IS_AI;

    // ── Lights ────────────────────────────────────────────────────────────────
    v.state.lights = LF_HEADLIGHTS;
    if (dv < -1.0f || v.ai_state == AIState::STOPPED)
        v.state.lights |= LF_BRAKE;

    // Update indicator
    v.indicator = computeIndicator(v);
    updateIndicatorLights(v, dt);
}

// ── Sensing: forward arc ──────────────────────────────────────────────────────
ObstacleInfo AITraffic::scanForwardArc(const AIVehicle& v) const {
    ObstacleInfo best;
    float fwd_x =  std::cos(v.heading);
    float fwd_y =  std::sin(v.heading);
    float rgt_x =  std::sin(v.heading);   // right-perpendicular
    float rgt_y = -std::cos(v.heading);

    // Check player vehicles
    for (auto& p : m_players) {
        float dx = p.pos[0] - v.state.pos[0];
        float dy = p.pos[1] - v.state.pos[1];
        float d  = std::sqrt(dx*dx + dy*dy);
        if (d < 0.5f || d > DETECT_DIST) continue;

        float dot_fwd = (dx * fwd_x + dy * fwd_y) / d;
        if (dot_fwd < DETECT_HALF_COS) continue;          // not in forward arc

        float lateral = dx * rgt_x + dy * rgt_y;
        if (std::fabs(lateral) > LANE_HALF_WIDTH + 1.0f) continue; // too far aside

        if (!best.detected || d < best.distance) {
            best.detected = true;
            best.distance = d;
            best.lateral  = lateral;
        }
    }

    // Check other AI vehicles in STOP zone to avoid rear-ends
    for (auto& other : m_vehicles) {
        if (!other.active || other.vehicle_id == v.vehicle_id) continue;
        float dx = other.state.pos[0] - v.state.pos[0];
        float dy = other.state.pos[1] - v.state.pos[1];
        float d  = std::sqrt(dx*dx + dy*dy);
        if (d < 0.5f || d > SLOW_DIST) continue;

        float dot_fwd = (dx * fwd_x + dy * fwd_y) / d;
        if (dot_fwd < DETECT_HALF_COS) continue;

        float lateral = dx * rgt_x + dy * rgt_y;
        if (std::fabs(lateral) > LANE_HALF_WIDTH) continue;

        if (!best.detected || d < best.distance) {
            best.detected = true;
            best.distance = d;
            best.lateral  = lateral;
        }
    }

    return best;
}

// ── Sensing: intersection ─────────────────────────────────────────────────────
bool AITraffic::intersectionAhead(const AIVehicle& v) const {
    if (v.path.empty() || v.path_step >= (int)v.path.size()) return false;
    const RoadNode& n = m_road_net.node(v.path[v.path_step]);
    if ((int)n.out_edges.size() < 3) return false;
    float d = dist2(v.state.pos[0], v.state.pos[1], n.x, n.y);
    return d < INDICATOR_DIST;
}

bool AITraffic::intersectionOccupied(int node_idx, const AIVehicle& self) const {
    const RoadNode& n = m_road_net.node(node_idx);
    for (auto& other : m_vehicles) {
        if (!other.active || other.vehicle_id == self.vehicle_id) continue;
        float d = dist2(other.state.pos[0], other.state.pos[1], n.x, n.y);
        if (d < INTERSECTION_STOP_D) return true;
    }
    return false;
}

// ── Indicators ────────────────────────────────────────────────────────────────
int AITraffic::computeIndicator(const AIVehicle& v) const {
    if (v.path.empty() || v.path_step >= (int)v.path.size()) return 0;

    const RoadNode& next = m_road_net.node(v.path[v.path_step]);
    float d_next = dist2(v.state.pos[0], v.state.pos[1], next.x, next.y);
    if (d_next > INDICATOR_DIST) return 0;

    // Need the node AFTER next to measure the turn
    int after_step = v.path_step + 1;
    if (after_step >= (int)v.path.size()) return 0;
    const RoadNode& after = m_road_net.node(v.path[after_step]);

    float bearing_next  = std::atan2(next.y  - v.state.pos[1], next.x  - v.state.pos[0]);
    float bearing_after = std::atan2(after.y - next.y,          after.x - next.x);
    float turn = wrap(bearing_after - bearing_next);

    if (turn >  INDICATOR_TURN_RAD) return -1; // turning left
    if (turn < -INDICATOR_TURN_RAD) return  1; // turning right
    return 0;
}

void AITraffic::updateIndicatorLights(AIVehicle& v, float dt) {
    if (v.indicator == 0) {
        // No turn needed — clear any active signal
        v.state.lights &= ~(uint8_t)(LF_SIGNAL_LEFT | LF_SIGNAL_RIGHT | LF_HAZARD);
        v.indicator_blink = 0.0f;
        return;
    }

    v.indicator_blink += INDICATOR_BLINK_HZ * dt;
    if (v.indicator_blink >= 1.0f) v.indicator_blink -= 1.0f;
    bool on = (v.indicator_blink < 0.5f);

    // Clear both signals then set the correct one
    v.state.lights &= ~(uint8_t)(LF_SIGNAL_LEFT | LF_SIGNAL_RIGHT | LF_HAZARD);
    if (on) {
        if (v.indicator == -1) v.state.lights |= LF_SIGNAL_LEFT;
        else                   v.state.lights |= LF_SIGNAL_RIGHT;
    }
}

// ── Path planning ─────────────────────────────────────────────────────────────
void AITraffic::planPath(AIVehicle& v) {
    v.replan_timer = m_cfg.ai_path_replan_secs;

    if (m_road_net.empty()) { v.path.clear(); return; }

    int from = m_road_net.nearestNode(v.state.pos[0], v.state.pos[1], v.state.pos[2]);
    int to;

    if (m_mode == "random") {
        to = m_road_net.randomNode(m_rng_state);
    } else {
        // Traffic mode: aim for a node near a random player to keep AI in active areas
        if (!m_players.empty()) {
            auto& pp = m_players[nextRng() % m_players.size()];
            to = m_road_net.nearestNode(pp.pos[0], pp.pos[1], pp.pos[2]);
            // Avoid staying in place
            if (to == from && m_road_net.nodeCount() > 1)
                to = m_road_net.randomNode(m_rng_state);
        } else {
            to = m_road_net.randomNode(m_rng_state);
        }
    }

    if (from < 0 || to < 0) { v.path.clear(); return; }

    auto path = m_road_net.findPath(from, to);
    if (path.empty() && from >= 0) {
        const auto& cn = m_road_net.node(from);
        if (!cn.out_edges.empty()) {
            int next = cn.out_edges[nextRng() % cn.out_edges.size()];
            path = {from, next};
        }
    }

    v.path      = std::move(path);
    v.path_step = (v.path.size() > 1) ? 1 : 0;
}

// ── Broadcast ─────────────────────────────────────────────────────────────────
void AITraffic::broadcastAll() {
    for (auto& v : m_vehicles) {
        if (!v.active) continue;
        v.state.vflags = VF_IS_AI;
        m_broadcast(Packet::buildVehicleUpdate(0xFFFF, ++m_seq, v.state));
    }
}

// ── Manual control ────────────────────────────────────────────────────────────
void AITraffic::spawnVehicle(const std::string& model, float x, float y, float z) {
    for (int i = 0; i < (int)m_vehicles.size(); ++i) {
        auto& v = m_vehicles[i];
        if (!v.active) {
            uint8_t saved_id = v.vehicle_id;
            v = {};
            v.vehicle_id   = saved_id;
            v.model        = model;
            v.state.pos[0] = x; v.state.pos[1] = y; v.state.pos[2] = z;
            v.state.rot[3] = 1.0f;
            v.state.vflags = VF_IS_AI;
            v.speed_target = m_speed_limit;
            v.active       = true;
            planPath(v);
            spdlog::info("AI: Spawned {} at {:.1f},{:.1f},{:.1f}", model, x, y, z);
            return;
        }
    }
    spdlog::warn("AI: All 55 slots full.");
}

void AITraffic::despawnVehicle(uint8_t id) {
    for (auto& v : m_vehicles) if (v.vehicle_id == id) { v.active = false; return; }
}
void AITraffic::despawnAll() {
    for (auto& v : m_vehicles) v.active = false;
    spdlog::info("AI: All vehicles despawned.");
}
void AITraffic::setCount(int count) {
    int excess = activeCount() - count;
    for (auto& v : m_vehicles)
        if (v.active && excess-- > 0) v.active = false;
    spdlog::info("AI: Count target = {}", count);
}
void AITraffic::setSpeedLimit(float mps) {
    m_speed_limit = mps;
    for (auto& v : m_vehicles) if (v.active) v.speed_target = mps;
    spdlog::info("AI: Speed limit = {:.1f} m/s", mps);
}
void AITraffic::setMode(const std::string& mode) {
    m_mode = mode;
    spdlog::info("AI: Mode = '{}'", mode);
}
void AITraffic::setPlayerInfo(const std::vector<PlayerInfo>& info) {
    m_players = info;
}

// ── Helpers ───────────────────────────────────────────────────────────────────
int AITraffic::activeCount() const {
    return (int)std::count_if(m_vehicles.begin(), m_vehicles.end(),
        [](const AIVehicle& v){ return v.active; });
}
float AITraffic::distToNearestPlayer(float x, float y, float z) const {
    float best = 1e9f;
    for (auto& p : m_players) {
        float d = dist3(x,y,z,p.pos[0],p.pos[1],p.pos[2]);
        if (d < best) best = d;
    }
    return best;
}
bool AITraffic::shouldDespawn(const AIVehicle& v) const {
    if (m_players.empty()) return false;
    return distToNearestPlayer(v.state.pos[0],v.state.pos[1],v.state.pos[2])
           > m_cfg.ai_despawn_dist;
}
std::array<float,3> AITraffic::spawnPositionNearPlayers() {
    if (m_players.empty()) {
        if (!m_road_net.empty()) {
            const auto& n = m_road_net.node(m_road_net.randomNode(m_rng_state));
            return {n.x, n.y, n.z};
        }
        return {0,0,0};
    }
    auto& pp  = m_players[nextRng() % m_players.size()];
    float ang = ((float)(nextRng() % 10000) / 10000.0f) * TWO_PI;
    float rad = m_cfg.ai_spawn_dist * 0.5f +
                ((float)(nextRng() % 10000) / 10000.0f) * m_cfg.ai_spawn_dist * 0.5f;
    float cx  = pp.pos[0] + rad * std::cos(ang);
    float cy  = pp.pos[1] + rad * std::sin(ang);
    if (!m_road_net.empty()) {
        int nn = m_road_net.nearestNode(cx, cy, pp.pos[2]);
        if (nn >= 0) {
            const auto& n = m_road_net.node(nn);
            return {n.x, n.y, n.z};
        }
    }
    return {cx, cy, pp.pos[2]};
}
std::string AITraffic::randomModel() {
    auto& pool = m_cfg.ai_vehicle_pool;
    return pool[nextRng() % pool.size()];
}

} // namespace novaMP
