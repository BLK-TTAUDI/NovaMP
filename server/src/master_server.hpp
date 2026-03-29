// server/src/master_server.hpp
#pragma once

#include <memory>
#include <atomic>
#include <toml++/toml.hpp>

#include "db/database.hpp"
#include "auth/auth_manager.hpp"
#include "registry/server_registry.hpp"
#include "api/rest_api.hpp"

namespace novaMP {

class MasterServer {
public:
    explicit MasterServer(const toml::table& config);
    ~MasterServer();

    void run();
    void stop();

    Database&       db()       { return *m_db; }
    AuthManager&    auth()     { return *m_auth; }
    ServerRegistry& registry() { return *m_registry; }

private:
    toml::table                     m_config;
    std::unique_ptr<Database>       m_db;
    std::unique_ptr<AuthManager>    m_auth;
    std::unique_ptr<ServerRegistry> m_registry;
    std::unique_ptr<RestAPI>        m_api;
    std::atomic<bool>               m_running{false};

    void cleanup_expired_servers();
    void stats_tick();
};

} // namespace novaMP
