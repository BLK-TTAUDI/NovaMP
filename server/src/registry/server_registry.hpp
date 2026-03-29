// server/src/registry/server_registry.hpp
#pragma once

#include <string>
#include <vector>
#include <optional>
#include "../db/database.hpp"

namespace novaMP {

struct GameServerEntry {
    int64_t     id;
    std::string name;
    std::string description;
    std::string host;
    int         port;
    std::string map;
    int         max_players;
    int         current_players;
    bool        password_protected;
    std::string version;
    std::string auth_token;
    std::string tags;
    std::string last_heartbeat;
};

class ServerRegistry {
public:
    ServerRegistry(Database& db, int heartbeat_timeout_secs);

    int64_t registerServer(const GameServerEntry& entry);
    bool    heartbeat(const std::string& auth_token,
                      int current_players, const std::string& map);
    bool    removeServer(const std::string& auth_token);
    int     removeExpired();

    std::vector<GameServerEntry>    getAll();
    std::optional<GameServerEntry>  getByToken(const std::string& auth_token);

private:
    Database& m_db;
    int       m_timeout;

    GameServerEntry rowToEntry(sqlite3_stmt* s);
};

} // namespace novaMP
