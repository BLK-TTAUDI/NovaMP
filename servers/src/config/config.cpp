// servers/src/config/config.cpp
#include "config.hpp"
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace novaMP {

ServerConfig ServerConfig::load(const std::string& path) {
    toml::table t;
    try { t = toml::parse_file(path); }
    catch (const toml::parse_error& e) {
        throw std::runtime_error(std::string("Config parse error: ") + e.what());
    }

    ServerConfig c;

    auto& srv = *t["server"].as_table();
    c.name        = srv["name"].value_or<std::string>("NovaMP Server");
    c.description = srv["description"].value_or<std::string>("");
    c.map         = srv["map"].value_or<std::string>("gridmap_v2");
    c.max_players = srv["max_players"].value_or(8);
    c.password    = srv["password"].value_or<std::string>("");
    c.port        = srv["port"].value_or(4444);
    c.log_level   = srv["log_level"].value_or<std::string>("info");
    c.log_file    = srv["log_file"].value_or<std::string>("server.log");
    if (auto* tags = t["server"]["tags"].as_array())
        tags->for_each([&](auto& el){ if (auto* s = el.as_string()) c.tags.push_back(**s); });

    auto& mst = *t["master"].as_table();
    c.master_register     = mst["register"].value_or(true);
    c.master_url          = mst["url"].value_or<std::string>("http://master.novaMP.gg");
    c.master_auth_token   = mst["auth_token"].value_or<std::string>("");
    c.master_heartbeat_hz = mst["heartbeat_hz"].value_or(0.033);

    auto& gp = *t["gameplay"].as_table();
    c.vehicle_limit_per_player = gp["vehicle_limit_per_player"].value_or(1);
    c.vehicle_sync_hz          = gp["vehicle_sync_hz"].value_or(100);
    c.max_vehicles             = gp["max_vehicles"].value_or(64);
    c.allow_vehicle_damage     = gp["allow_vehicle_damage"].value_or(true);
    c.respawn_on_disconnect    = gp["respawn_on_disconnect"].value_or(false);

    auto& ai = *t["ai_traffic"].as_table();
    c.ai_enabled          = ai["enabled"].value_or(false);
    c.ai_count            = ai["count"].value_or(10);
    c.ai_speed_limit      = (float)ai["speed_limit"].value_or(14.0);
    c.ai_mode             = ai["mode"].value_or<std::string>("traffic");
    c.ai_update_rate_hz   = ai["update_rate_hz"].value_or(20);
    c.ai_despawn_dist     = (float)ai["despawn_dist"].value_or(500.0);
    c.ai_spawn_dist       = (float)ai["spawn_dist"].value_or(300.0);
    c.ai_path_replan_secs = (float)ai["path_replan_secs"].value_or(10.0);
    if (auto* pool = ai["vehicle_pool"].as_array())
        pool->for_each([&](auto& el){ if (auto* s = el.as_string()) c.ai_vehicle_pool.push_back(**s); });
    if (c.ai_vehicle_pool.empty()) c.ai_vehicle_pool = {"etk800","vivace","pessima"};

    // [ai_authority]  (optional section — all keys have defaults)
    if (t.contains("ai_authority")) {
        auto& auth = *t["ai_authority"].as_table();
        c.authority_mode        = auth["mode"].value_or<std::string>("auto");
        c.beamng_exe_path       = auth["beamng_exe_path"].value_or<std::string>("");
        c.authority_timeout_sec = auth["timeout_sec"].value_or(15);
        c.bridge_token          = auth["bridge_token"].value_or<std::string>("changeme_bridge");
    } else {
        c.authority_mode        = "auto";
        c.beamng_exe_path       = "";
        c.authority_timeout_sec = 15;
        c.bridge_token          = "changeme_bridge";
    }

    auto& rcon = *t["rcon"].as_table();
    c.rcon_enabled  = rcon["enabled"].value_or(false);
    c.rcon_port     = rcon["port"].value_or(4445);
    c.rcon_password = rcon["password"].value_or<std::string>("changeme");

    auto& res = *t["resources"].as_table();
    c.resources_dir         = res["directory"].value_or<std::string>("Resources/Server");
    c.resources_auto_reload = res["auto_reload"].value_or(false);

    spdlog::info("Config loaded: map={}, players={}, ai={}",
                 c.map, c.max_players, c.ai_enabled ? "ON" : "OFF");
    return c;
}

} // namespace novaMP
