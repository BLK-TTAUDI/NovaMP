// server/src/registry/server_registry.cpp
#include "server_registry.hpp"
#include <spdlog/spdlog.h>

namespace novaMP {

ServerRegistry::ServerRegistry(Database& db, int heartbeat_timeout_secs)
    : m_db(db), m_timeout(heartbeat_timeout_secs) {}

GameServerEntry ServerRegistry::rowToEntry(sqlite3_stmt* s) {
    GameServerEntry e;
    e.id                 = Database::col_int64(s, 0);
    e.name               = Database::col_text(s, 1);
    e.description        = Database::col_text(s, 2);
    e.host               = Database::col_text(s, 3);
    e.port               = Database::col_int(s, 4);
    e.map                = Database::col_text(s, 5);
    e.max_players        = Database::col_int(s, 6);
    e.current_players    = Database::col_int(s, 7);
    e.password_protected = Database::col_int(s, 8) != 0;
    e.version            = Database::col_text(s, 9);
    e.auth_token         = Database::col_text(s, 10);
    e.tags               = Database::col_text(s, 11);
    e.last_heartbeat     = Database::col_text(s, 12);
    return e;
}

int64_t ServerRegistry::registerServer(const GameServerEntry& entry) {
    int64_t id = m_db.insert(
        "INSERT INTO game_servers"
        "(name,description,host,port,map,max_players,current_players,"
        " password_protected,version,auth_token,tags,last_heartbeat) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,datetime('now')) "
        "ON CONFLICT(auth_token) DO UPDATE SET "
        "  name=excluded.name, description=excluded.description, "
        "  host=excluded.host, port=excluded.port, map=excluded.map, "
        "  max_players=excluded.max_players, "
        "  current_players=excluded.current_players, "
        "  last_heartbeat=excluded.last_heartbeat",
        entry.name, entry.description, entry.host, entry.port,
        entry.map, entry.max_players, entry.current_players,
        (int)entry.password_protected, entry.version,
        entry.auth_token, entry.tags);

    spdlog::info("Server registered: '{}' @ {}:{} (id={})",
                 entry.name, entry.host, entry.port, id);
    return id;
}

bool ServerRegistry::heartbeat(const std::string& auth_token,
                                int current_players,
                                const std::string& map) {
    bool found = false;
    m_db.query("SELECT 1 FROM game_servers WHERE auth_token=? LIMIT 1",
               [&](sqlite3_stmt*){ found = true; }, auth_token);
    if (!found) return false;

    m_db.exec(
        "UPDATE game_servers SET "
        "  current_players=?, map=?, last_heartbeat=datetime('now') "
        "WHERE auth_token=?",
        current_players, map, auth_token);
    return true;
}

bool ServerRegistry::removeServer(const std::string& auth_token) {
    m_db.exec("DELETE FROM game_servers WHERE auth_token=?", auth_token);
    spdlog::info("Server unregistered (token={}...)", auth_token.substr(0, 8));
    return true;
}

int ServerRegistry::removeExpired() {
    std::string cutoff = std::to_string(m_timeout);
    int before = 0, after = 0;
    m_db.query("SELECT COUNT(*) FROM game_servers",
               [&](sqlite3_stmt* s){ before = Database::col_int(s, 0); });
    m_db.exec("DELETE FROM game_servers WHERE "
              "  last_heartbeat < datetime('now', '-' || ? || ' seconds')", cutoff);
    m_db.query("SELECT COUNT(*) FROM game_servers",
               [&](sqlite3_stmt* s){ after = Database::col_int(s, 0); });
    return before - after;
}

std::vector<GameServerEntry> ServerRegistry::getAll() {
    std::vector<GameServerEntry> result;
    m_db.query(
        "SELECT id,name,description,host,port,map,max_players,current_players,"
        "  password_protected,version,auth_token,tags,last_heartbeat "
        "FROM game_servers ORDER BY current_players DESC, name ASC",
        [&](sqlite3_stmt* s){ result.push_back(rowToEntry(s)); });
    return result;
}

std::optional<GameServerEntry> ServerRegistry::getByToken(const std::string& auth_token) {
    std::optional<GameServerEntry> result;
    m_db.query(
        "SELECT id,name,description,host,port,map,max_players,current_players,"
        "  password_protected,version,auth_token,tags,last_heartbeat "
        "FROM game_servers WHERE auth_token=? LIMIT 1",
        [&](sqlite3_stmt* s){ result = rowToEntry(s); }, auth_token);
    return result;
}

} // namespace novaMP
