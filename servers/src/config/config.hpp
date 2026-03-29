// servers/src/config/config.hpp
#pragma once

#include <string>
#include <vector>
#include <toml++/toml.hpp>

namespace novaMP {

struct ServerConfig {
    // [server]
    std::string name, description, map, password, log_level, log_file;
    int         max_players, port;
    std::vector<std::string> tags;

    // [master]
    bool        master_register;
    std::string master_url, master_auth_token;
    double      master_heartbeat_hz;

    // [gameplay]
    int  vehicle_limit_per_player, vehicle_sync_hz, max_vehicles;
    bool allow_vehicle_damage, respawn_on_disconnect;

    // [ai_traffic]
    bool                     ai_enabled;
    int                      ai_count, ai_update_rate_hz;
    float                    ai_speed_limit, ai_despawn_dist, ai_spawn_dist, ai_path_replan_secs;
    std::string              ai_mode;
    std::vector<std::string> ai_vehicle_pool;

    // [ai_authority]
    // Controls how the server sources BeamNG AI states.
    //   "auto"     — try headless BeamNG first, then client volunteer, then builtin
    //   "headless" — headless BeamNG only (fail if unavailable)
    //   "client"   — client volunteer only (fail if nobody volunteers)
    //   "builtin"  — always use the C++ waypoint AI (original behaviour)
    std::string authority_mode;        // default "auto"
    std::string beamng_exe_path;       // path to BeamNG.drive.exe; empty = auto-detect
    int         authority_timeout_sec; // seconds to wait for headless/client before falling back
    std::string bridge_token;          // shared secret used by the ai_bridge Lua mod to auth

    // [rcon]
    bool        rcon_enabled;
    int         rcon_port;
    std::string rcon_password;

    // [resources]
    std::string resources_dir;
    bool        resources_auto_reload;

    static ServerConfig load(const std::string& path);
};

} // namespace novaMP
