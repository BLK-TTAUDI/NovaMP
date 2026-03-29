// server/src/master_server.cpp
#include "master_server.hpp"

#include <thread>
#include <chrono>
#include <spdlog/spdlog.h>

namespace novaMP {

MasterServer::MasterServer(const toml::table& config)
    : m_config(config)
{
    spdlog::info("Initializing master server components...");

    auto db_cfg = m_config["database"];
    std::string db_type = db_cfg["type"].value_or<std::string>("sqlite");
    if (db_type == "sqlite") {
        auto path = db_cfg["sqlite_path"].value_or<std::string>("data/novaMP.db");
        m_db = std::make_unique<Database>(path);
    } else {
        throw std::runtime_error("Only SQLite is currently supported");
    }

    m_db->initialize("schema.sql");

    auto auth_cfg = m_config["auth"];
    AuthConfig ac{
        .jwt_secret       = auth_cfg["jwt_secret"].value_or<std::string>("CHANGEME"),
        .jwt_expiry_hours = auth_cfg["jwt_expiry_hours"].value_or(72),
        .bcrypt_rounds    = auth_cfg["bcrypt_rounds"].value_or(12),
        .discord_id       = auth_cfg["discord_client_id"].value_or<std::string>(""),
        .discord_secret   = auth_cfg["discord_secret"].value_or<std::string>(""),
        .discord_redirect = auth_cfg["discord_redirect"].value_or<std::string>("")
    };
    m_auth = std::make_unique<AuthManager>(*m_db, ac);

    int heartbeat_timeout = m_config["server_browser"]["heartbeat_timeout"].value_or(60);
    m_registry = std::make_unique<ServerRegistry>(*m_db, heartbeat_timeout);

    auto srv_cfg = m_config["server"];
    std::string host = srv_cfg["host"].value_or<std::string>("0.0.0.0");
    int         port = srv_cfg["port"].value_or(8080);
    m_api = std::make_unique<RestAPI>(*this, host, port);

    spdlog::info("Master server initialized.");
}

MasterServer::~MasterServer() = default;

void MasterServer::run() {
    m_running = true;
    spdlog::info("Master server running.");

    std::thread cleanup_thread([this] {
        while (m_running) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            if (!m_running) break;
            try {
                cleanup_expired_servers();
                stats_tick();
            } catch (const std::exception& e) {
                spdlog::error("Cleanup error: {}", e.what());
            }
        }
    });

    m_api->run();

    m_running = false;
    if (cleanup_thread.joinable()) cleanup_thread.join();
}

void MasterServer::stop() {
    spdlog::info("Stopping master server...");
    m_running = false;
    m_api->stop();
}

void MasterServer::cleanup_expired_servers() {
    int removed = m_registry->removeExpired();
    if (removed > 0)
        spdlog::debug("Removed {} expired game servers from registry.", removed);
}

void MasterServer::stats_tick() {
    auto servers = m_registry->getAll();
    int total_players = 0;
    for (auto& s : servers) total_players += s.current_players;

    m_db->exec(
        "INSERT INTO stats(total_players, total_servers) VALUES (?, ?)",
        total_players, (int)servers.size()
    );
}

} // namespace novaMP
