// servers/src/sync/road_network.hpp
//
// RoadNetwork — loads and queries a road graph for server-side AI navigation.
//
// Supported input formats (all stored in Resources/Server/maps/<map>/):
//   1. roads.json — BeamNG AI waypoint export from the level editor
//      { "class":"AIWaypoints", "waypoints": { "id": { "pos":[x,y,z],
//        "links":["id2",...], "speed":13.9, "drivability":1.0 } } }
//   2. waypoints.json — NovaMP simple format (integer-indexed)
//      { "waypoints": [ { "x":0, "y":0, "z":0, "neighbors":[1,2],
//        "speed_limit":14.0, "width":4.0 } ] }
//   3. Procedural fallback — 400×400 m grid if neither file is found.
//
// Path planning uses A* with Euclidean heuristic.
// Nearest-node queries use a grid spatial index (O(1) average).
#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <array>
#include <cstdint>

namespace novaMP {

struct RoadNode {
    float x, y, z;
    float speed_limit = 14.0f;  // m/s (default ~50 km/h)
    float lane_width  = 3.5f;   // m
    std::vector<int> out_edges; // indices into RoadNetwork::m_nodes
};

class RoadNetwork {
public:
    RoadNetwork() = default;

    // Load from file. Returns true on success (falls back to procedural on fail).
    bool load(const std::string& map_dir);

    // A* shortest path between two node indices. Returns [] if unreachable.
    std::vector<int> findPath(int from_idx, int to_idx) const;

    // Nearest node to a world position — O(1) average via spatial grid.
    int nearestNode(float x, float y, float z) const;

    // Random node index (for picking random destinations)
    int randomNode(uint32_t& rng_state) const;

    const RoadNode& node(int idx) const { return m_nodes[idx]; }
    int             nodeCount()   const { return (int)m_nodes.size(); }
    bool            empty()       const { return m_nodes.empty(); }

private:
    std::vector<RoadNode> m_nodes;

    // Spatial grid for fast nearest-node lookup
    struct GridCell { std::vector<int> indices; };
    std::unordered_map<uint64_t, GridCell> m_grid;
    float m_grid_cell_size = 30.0f;
    float m_grid_origin_x  = 0.0f;
    float m_grid_origin_y  = 0.0f;

    void buildSpatialIndex();
    uint64_t cellKey(int gx, int gy) const;

    bool loadBeamNGFormat(const std::string& path);  // roads.json (AI waypoints)
    bool loadNovaFormat  (const std::string& path);  // waypoints.json
    void buildProceduralGrid();

    float heuristic(int a, int b) const;
    float edgeDist  (int a, int b) const;
};

} // namespace novaMP
