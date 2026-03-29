// server/src/db/database.cpp
#include "database.hpp"

#include <fstream>
#include <sstream>
#include <spdlog/spdlog.h>

namespace novaMP {

Database::Database(const std::string& path) {
    int rc = sqlite3_open(path.c_str(), &m_db);
    if (rc != SQLITE_OK)
        throw std::runtime_error(std::string("Cannot open database: ") + sqlite3_errmsg(m_db));

    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;",    nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA synchronous=NORMAL;",  nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA foreign_keys=ON;",     nullptr, nullptr, nullptr);
    sqlite3_busy_timeout(m_db, 5000);
    spdlog::info("Database opened: {}", path);
}

Database::~Database() {
    if (m_db) sqlite3_close(m_db);
}

void Database::initialize(const std::string& schema_path) {
    std::ifstream f(schema_path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open schema file: " + schema_path);
    std::ostringstream ss;
    ss << f.rdbuf();

    char* errmsg = nullptr;
    int rc = sqlite3_exec(m_db, ss.str().c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string err = errmsg ? errmsg : "unknown";
        sqlite3_free(errmsg);
        throw std::runtime_error("Schema init failed: " + err);
    }
    spdlog::info("Database schema applied.");
}

Database::StmtPtr Database::prepare(const std::string& sql) {
    std::lock_guard lk(m_mutex);
    sqlite3_stmt* raw = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &raw, nullptr);
    if (rc != SQLITE_OK)
        throw std::runtime_error(std::string("Prepare failed: ") + sqlite3_errmsg(m_db));
    return StmtPtr(raw);
}

void Database::step_no_result(StmtPtr& stmt) {
    int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE && rc != SQLITE_ROW)
        throw std::runtime_error(std::string("Step failed: ") + sqlite3_errmsg(m_db));
}

// Explicit bind specialisations
template<>
void Database::bind_one<std::string>(sqlite3_stmt* s, int i, std::string&& v) {
    sqlite3_bind_text(s, i, v.c_str(), -1, SQLITE_TRANSIENT);
}
template<>
void Database::bind_one<int>(sqlite3_stmt* s, int i, int&& v) {
    sqlite3_bind_int(s, i, v);
}
template<>
void Database::bind_one<int64_t>(sqlite3_stmt* s, int i, int64_t&& v) {
    sqlite3_bind_int64(s, i, v);
}
template<>
void Database::bind_one<double>(sqlite3_stmt* s, int i, double&& v) {
    sqlite3_bind_double(s, i, v);
}

} // namespace novaMP
