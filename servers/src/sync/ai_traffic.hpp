// servers/src/sync/ai_traffic.hpp
//
// Server-Side Authoritative AI Traffic — Smart Edition
//
// Each AI vehicle runs a 4-state finite state machine:
//   DRIVING   — normal path following at speed limit
//   SLOWING   — player detected in forward arc (≤40 m), speed scaling with distance
//   STOPPED   — player within stop zone (≤8 m in lane), fully braked
//   REROUTING — blocked >8 s, A* replanning to a different goal
//
// Indicators:
//   Looks 1-2 nodes ahead on the planned path.  Activates LF_SIGNAL_LEFT /
//   LF_SIGNAL_RIGHT when a significant turn (>20°) is within 25 m.
//   Blinks at 1.5 Hz using state.lights toggling.
//
// Lateral avoidance:
//   When an obstacle (player) is within AVOID_DIST (15 m) ahead, the AI
//   computes which side of the lane the obstacle is in and applies a
//   perpendicular offset to its steering target, effectively going around them.
//
// Intersection yielding:
//   Nodes with 3+ outgoing edges are flagged as intersections.  The AI slows
//   to 60% speed on approach, and if another AI already occupies the node
//   it waits at the edge.
#pragma once

#include <vector>
#include <string>
#include <functional>
#include <array>
#include <cstdint>
#include "../network/packet.hpp"
#include "road_network.hpp"

namespace novaMP {

struct ServerConfig;

// ── Player data passed in from GameServer each tick ───────────────────────────
struct PlayerInfo {
    float pos[3]  = {};
    float vel[3]  = {};    // last-known velocity vector
    float heading = 0.0f;  // yaw from velocity, radians
    float speed   = 0.0f;  // scalar m/s
};

// ── Per-vehicle state machine ─────────────────────────────────────────────────
enum class AIState : uint8_t {
    DRIVING,    // Normal operation
    SLOWING,    // Player in forward arc; proportional speed reduction
    STOPPED,    // Player in stop zone; fully braked; waiting
    REROUTING,  // Blocked too long; replanning to a new destination
};

// ── AI vehicle ────────────────────────────────────────────────────────────────
struct AIVehicle {
    uint8_t     vehicle_id = 0;  // 200–254
    std::string model;
    VehicleState state{};

    // Ackermann kinematics
    float heading = 0.0f;   // world yaw, radians
    float speed   = 0.0f;   // current scalar m/s

    // Path planning
    std::vector<int> path;
    int   path_step    = 0;
    float replan_timer = 0.0f;

    // Speed intent (set from config + per-node limit)
    float speed_target = 0.0f;

    // Behaviour state machine
    AIState ai_state      = AIState::DRIVING;
    float   state_timer   = 0.0f;  // seconds in current state
    float   blocked_timer = 0.0f;  // cumulative blocking time

    // Indicators
    int   indicator       = 0;     // -1=left, 0=none, +1=right
    float indicator_blink = 0.0f;  // phase accumulator (0–1)

    // Lateral avoidance
    float lateral_offset       = 0.0f;  // metres; +right, -left of heading
    float lateral_offset_timer = 0.0f;  // used to smooth transition

    bool active = false;
};

// ── Forward-scan result ───────────────────────────────────────────────────────
struct ObstacleInfo {
    bool  detected = false;
    float distance = 9999.0f;
    float lateral  = 0.0f;    // +right, -left of AI heading
};

// ── AITraffic class ───────────────────────────────────────────────────────────
class AITraffic {
public:
    using BroadcastFn = std::function<void(const std::vector<uint8_t>&)>;

    AITraffic(const ServerConfig& cfg, BroadcastFn broadcast);

    void loadRoadNetwork(const std::string& map_name);

    void tick(float dt);
    void broadcastAll();

    void spawnVehicle(const std::string& model, float x, float y, float z);
    void despawnVehicle(uint8_t vehicle_id);
    void despawnAll();
    void setCount      (int count);
    void setSpeedLimit (float mps);
    void setMode       (const std::string& mode);

    // Called by GameServer::aiTrafficLoop — replaces old setPlayerPositions
    void setPlayerInfo(const std::vector<PlayerInfo>& info);

    const std::vector<AIVehicle>& vehicles()    const { return m_vehicles; }
    int                           activeCount() const;
    const RoadNetwork&            roadNetwork() const { return m_road_net; }

private:
    const ServerConfig& m_cfg;
    BroadcastFn         m_broadcast;
    std::vector<AIVehicle>  m_vehicles;
    RoadNetwork             m_road_net;
    std::vector<PlayerInfo> m_players;

    uint32_t    m_rng_state{12345};
    float       m_speed_limit;
    std::string m_mode;
    uint32_t    m_seq = 0;

    // ── Per-vehicle update ────────────────────────────────────────────────────
    void tickVehicle(AIVehicle& v, float dt);
    void planPath   (AIVehicle& v);

    // ── Sensing ───────────────────────────────────────────────────────────────
    ObstacleInfo  scanForwardArc(const AIVehicle& v) const;
    bool          intersectionAhead(const AIVehicle& v) const;
    bool          intersectionOccupied(int node_idx, const AIVehicle& self) const;

    // ── Indicators ───────────────────────────────────────────────────────────
    int  computeIndicator(const AIVehicle& v) const;
    void updateIndicatorLights(AIVehicle& v, float dt);

    // ── Helpers ───────────────────────────────────────────────────────────────
    float distToNearestPlayer(float x, float y, float z) const;
    bool  shouldDespawn(const AIVehicle& v) const;
    std::array<float,3> spawnPositionNearPlayers();
    std::string randomModel();
    uint32_t nextRng() {
        m_rng_state ^= m_rng_state << 13;
        m_rng_state ^= m_rng_state >> 17;
        m_rng_state ^= m_rng_state <<  5;
        return m_rng_state;
    }
};

} // namespace novaMP
