// servers/src/sync/road_network.cpp
#include "road_network.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <cmath>
#include <queue>
#include <algorithm>
#include <limits>

namespace novaMP {
using json = nlohmann::json;

// ── Utility ───────────────────────────────────────────────────────────────────
static float dist3f(float ax, float ay, float az,
                    float bx, float by, float bz) {
    float dx = ax-bx, dy = ay-by, dz = az-bz;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

// Fast xorshift32 used only in randomNode
static uint32_t xorshift(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s;
}

// ── Public ────────────────────────────────────────────────────────────────────
bool RoadNetwork::load(const std::string& map_dir) {
    // Try BeamNG AI waypoint export first (roads.json)
    if (loadBeamNGFormat(map_dir + "/roads.json")) return true;
    // Then our simple format
    if (loadNovaFormat  (map_dir + "/waypoints.json")) return true;
    // Fall back to procedural
    spdlog::warn("[RoadNet] No road data for '{}' — building procedural grid.", map_dir);
    buildProceduralGrid();
    return false; // indicates fallback
}

// ── BeamNG AI waypoint export parser ─────────────────────────────────────────
// Format exported by BeamNG's AI Waypoint editor (File → Export AI Waypoints):
//   {
//     "class": "AIWaypoints",
//     "waypoints": {
//       "wp_0": { "pos": [x,y,z], "links": ["wp_1","wp_3"],
//                 "speed": 13.9, "drivability": 1.0, "width": 3.5 },
//       ...
//     }
//   }
bool RoadNetwork::loadBeamNGFormat(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    json j;
    try { j = json::parse(f); }
    catch (...) { return false; }

    // Accept both "class":"AIWaypoints" and bare {"waypoints":{...}} objects
    if (!j.contains("waypoints")) return false;
    auto& wps = j["waypoints"];
    if (!wps.is_object()) return false;

    // Two-pass: first assign indices, then resolve link names → indices
    std::unordered_map<std::string, int> id_to_idx;
    m_nodes.clear();

    for (auto& [id, wp] : wps.items()) {
        RoadNode n;
        if (wp.contains("pos") && wp["pos"].is_array() && wp["pos"].size() >= 3) {
            n.x = wp["pos"][0].get<float>();
            n.y = wp["pos"][1].get<float>();
            n.z = wp["pos"][2].get<float>();
        } else {
            // Alternate: pos as object {x,y,z}
            n.x = wp.value("x", 0.0f);
            n.y = wp.value("y", 0.0f);
            n.z = wp.value("z", 0.0f);
        }
        n.speed_limit = wp.value("speed", 14.0f);
        n.lane_width  = wp.value("width", 3.5f);

        int idx = (int)m_nodes.size();
        id_to_idx[id] = idx;
        m_nodes.push_back(n);
    }

    // Second pass: resolve links
    int idx = 0;
    for (auto& [id, wp] : wps.items()) {
        if (wp.contains("links") && wp["links"].is_array()) {
            for (auto& lnk : wp["links"]) {
                std::string lid = lnk.get<std::string>();
                auto it = id_to_idx.find(lid);
                if (it != id_to_idx.end())
                    m_nodes[idx].out_edges.push_back(it->second);
            }
        }
        ++idx;
    }

    buildSpatialIndex();
    spdlog::info("[RoadNet] Loaded {} nodes from BeamNG format: {}", m_nodes.size(), path);
    return !m_nodes.empty();
}

// ── NovaMP simple format parser ───────────────────────────────────────────────
// { "waypoints": [ { "x":0, "y":0, "z":0,
//                    "neighbors":[1,2], "speed_limit":14.0, "width":3.5 } ] }
bool RoadNetwork::loadNovaFormat(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    json j;
    try { j = json::parse(f); }
    catch (...) { return false; }

    if (!j.contains("waypoints") || !j["waypoints"].is_array()) return false;
    m_nodes.clear();

    for (auto& wp : j["waypoints"]) {
        RoadNode n;
        n.x           = wp.value("x", 0.0f);
        n.y           = wp.value("y", 0.0f);
        n.z           = wp.value("z", 0.0f);
        n.speed_limit = wp.value("speed_limit", 14.0f);
        n.lane_width  = wp.value("width", 3.5f);
        for (auto& nb : wp.value("neighbors", json::array()))
            n.out_edges.push_back(nb.get<int>());
        m_nodes.push_back(n);
    }

    buildSpatialIndex();
    spdlog::info("[RoadNet] Loaded {} nodes from Nova format: {}", m_nodes.size(), path);
    return !m_nodes.empty();
}

// ── Procedural 400×400 m grid fallback ───────────────────────────────────────
void RoadNetwork::buildProceduralGrid() {
    m_nodes.clear();
    const int STEP = 20, HALF = 200;
    const int COLS = (2*HALF/STEP) + 1;

    for (int yi = 0; yi <= 2*HALF/STEP; ++yi)
    for (int xi = 0; xi <= 2*HALF/STEP; ++xi) {
        RoadNode n;
        n.x           = (float)(xi * STEP - HALF);
        n.y           = (float)(yi * STEP - HALF);
        n.z           = 0.0f;
        n.speed_limit = 14.0f;
        n.lane_width  = 3.5f;
        int idx = (int)m_nodes.size();
        // Bidirectional links to left and bottom neighbours
        if (xi > 0) {
            n.out_edges.push_back(idx-1);
            m_nodes[idx-1].out_edges.push_back(idx);
        }
        if (yi > 0) {
            n.out_edges.push_back(idx-COLS);
            m_nodes[idx-COLS].out_edges.push_back(idx);
        }
        m_nodes.push_back(n);
    }

    buildSpatialIndex();
    spdlog::info("[RoadNet] Procedural grid: {} nodes", m_nodes.size());
}

// ── Spatial index ─────────────────────────────────────────────────────────────
uint64_t RoadNetwork::cellKey(int gx, int gy) const {
    return ((uint64_t)(uint32_t)gx << 32) | (uint32_t)gy;
}

void RoadNetwork::buildSpatialIndex() {
    m_grid.clear();
    for (int i = 0; i < (int)m_nodes.size(); ++i) {
        int gx = (int)std::floor((m_nodes[i].x - m_grid_origin_x) / m_grid_cell_size);
        int gy = (int)std::floor((m_nodes[i].y - m_grid_origin_y) / m_grid_cell_size);
        m_grid[cellKey(gx, gy)].indices.push_back(i);
    }
}

// ── Nearest node ──────────────────────────────────────────────────────────────
int RoadNetwork::nearestNode(float x, float y, float z) const {
    if (m_nodes.empty()) return -1;

    int gx = (int)std::floor((x - m_grid_origin_x) / m_grid_cell_size);
    int gy = (int)std::floor((y - m_grid_origin_y) / m_grid_cell_size);

    // Search expanding rings of cells until we find at least one candidate
    int best = -1;
    float best_d = std::numeric_limits<float>::max();

    for (int radius = 0; radius <= 4 && best < 0; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            if (std::abs(dx) != radius && std::abs(dy) != radius) continue;
            auto it = m_grid.find(cellKey(gx+dx, gy+dy));
            if (it == m_grid.end()) continue;
            for (int idx : it->second.indices) {
                float d = dist3f(x, y, z,
                    m_nodes[idx].x, m_nodes[idx].y, m_nodes[idx].z);
                if (d < best_d) { best_d = d; best = idx; }
            }
        }
        // Once we've searched radius 0+1 and found something, the cell-size
        // guarantee means nothing closer can be in a farther ring.
        if (best >= 0 && radius >= 1) break;
    }

    // Final fallback: linear scan (very large open areas with sparse nodes)
    if (best < 0) {
        for (int i = 0; i < (int)m_nodes.size(); ++i) {
            float d = dist3f(x, y, z,
                m_nodes[i].x, m_nodes[i].y, m_nodes[i].z);
            if (d < best_d) { best_d = d; best = i; }
        }
    }
    return best;
}

// ── Random node ───────────────────────────────────────────────────────────────
int RoadNetwork::randomNode(uint32_t& s) const {
    if (m_nodes.empty()) return 0;
    return (int)(xorshift(s) % (uint32_t)m_nodes.size());
}

// ── A* pathfinding ────────────────────────────────────────────────────────────
float RoadNetwork::heuristic(int a, int b) const {
    return dist3f(m_nodes[a].x, m_nodes[a].y, m_nodes[a].z,
                  m_nodes[b].x, m_nodes[b].y, m_nodes[b].z);
}

float RoadNetwork::edgeDist(int a, int b) const {
    return dist3f(m_nodes[a].x, m_nodes[a].y, m_nodes[a].z,
                  m_nodes[b].x, m_nodes[b].y, m_nodes[b].z);
}

std::vector<int> RoadNetwork::findPath(int from, int to) const {
    if (from < 0 || to < 0 || from >= (int)m_nodes.size()
                            || to   >= (int)m_nodes.size()) return {};
    if (from == to) return {from};

    struct State {
        float f; int node;
        bool operator>(const State& o) const { return f > o.f; }
    };

    std::vector<float> g(m_nodes.size(), std::numeric_limits<float>::max());
    std::vector<int>   came_from(m_nodes.size(), -1);
    std::priority_queue<State, std::vector<State>, std::greater<State>> open;

    g[from] = 0.0f;
    open.push({heuristic(from, to), from});

    while (!open.empty()) {
        auto [f, cur] = open.top(); open.pop();

        if (cur == to) {
            std::vector<int> path;
            for (int n = to; n != -1; n = came_from[n]) path.push_back(n);
            std::reverse(path.begin(), path.end());
            return path;
        }

        float cur_g = g[cur];
        if (f > cur_g + heuristic(cur, to) + 0.001f) continue; // stale

        for (int nb : m_nodes[cur].out_edges) {
            float ng = cur_g + edgeDist(cur, nb);
            if (ng < g[nb]) {
                g[nb]         = ng;
                came_from[nb] = cur;
                open.push({ng + heuristic(nb, to), nb});
            }
        }
    }
    return {}; // unreachable
}

} // namespace novaMP
