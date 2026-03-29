// server/src/db/database.hpp
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include <memory>
#include <mutex>
#include <sqlite3.h>

namespace novaMP {

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    void initialize(const std::string& schema_path);

    template<typename... Args>
    void exec(const std::string& sql, Args&&... args) {
        auto stmt = prepare(sql);
        bind(stmt, 1, std::forward<Args>(args)...);
        step_no_result(stmt);
    }

    template<typename... Args>
    int64_t insert(const std::string& sql, Args&&... args) {
        auto stmt = prepare(sql);
        bind(stmt, 1, std::forward<Args>(args)...);
        step_no_result(stmt);
        return sqlite3_last_insert_rowid(m_db);
    }

    template<typename... Args>
    void query(const std::string& sql,
               std::function<void(sqlite3_stmt*)> row_cb,
               Args&&... args)
    {
        auto stmt = prepare(sql);
        bind(stmt, 1, std::forward<Args>(args)...);
        int rc;
        while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW)
            row_cb(stmt.get());
        if (rc != SQLITE_DONE)
            throw std::runtime_error(sqlite3_errmsg(m_db));
    }

    static std::string col_text (sqlite3_stmt* s, int i) {
        auto p = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
        return p ? p : "";
    }
    static int     col_int  (sqlite3_stmt* s, int i) { return sqlite3_column_int(s, i); }
    static int64_t col_int64(sqlite3_stmt* s, int i) { return sqlite3_column_int64(s, i); }
    static double  col_dbl  (sqlite3_stmt* s, int i) { return sqlite3_column_double(s, i); }

private:
    sqlite3*   m_db = nullptr;
    std::mutex m_mutex;

    struct StmtDeleter { void operator()(sqlite3_stmt* s){ sqlite3_finalize(s); } };
    using StmtPtr = std::unique_ptr<sqlite3_stmt, StmtDeleter>;

    StmtPtr prepare(const std::string& sql);
    void    step_no_result(StmtPtr& stmt);

    template<typename T, typename... Rest>
    void bind_one(sqlite3_stmt* s, int i, T&& val, Rest&&... rest);

    template<typename... Args>
    void bind(StmtPtr& stmt, int start, Args&&... args) {
        bind_one(stmt.get(), start, std::forward<Args>(args)...);
    }
    void bind(StmtPtr&, int) {}
};

} // namespace novaMP
