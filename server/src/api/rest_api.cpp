// server/src/api/rest_api.cpp
#include "rest_api.hpp"
#include "../master_server.hpp"

#include <crow.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace novaMP {
namespace {

using json = nlohmann::json;

crow::response ok_resp(const json& body) {
    auto res = crow::response(200, body.dump());
    res.add_header("Content-Type", "application/json");
    return res;
}
crow::response err_resp(int code, const std::string& msg) {
    auto res = crow::response(code, json{{"error", msg}}.dump());
    res.add_header("Content-Type", "application/json");
    return res;
}

} // anonymous namespace

struct RestAPI::Impl {
    crow::SimpleApp app;
    MasterServer&   master;
    std::string     host;
    int             port;

    Impl(MasterServer& m, const std::string& h, int p)
        : master(m), host(h), port(p)
    { setup_routes(); }

    void setup_routes() {
        // Health
        CROW_ROUTE(app, "/health")([] {
            return ok_resp({{"status","ok"},{"service","novaMP-master"}});
        });

        // Register
        CROW_ROUTE(app, "/auth/register").methods("POST"_method)
        ([this](const crow::request& req) {
            auto body = json::parse(req.body, nullptr, false);
            if (body.is_discarded()) return err_resp(400, "Invalid JSON");
            auto result = master.auth().registerUser(
                body.value("username",""),
                body.value("email",""),
                body.value("password",""));
            if (!result.success) return err_resp(409, result.error);
            return ok_resp({{"token",result.token},{"refresh",result.refresh_token},
                {"user",{{"id",result.user.id},{"username",result.user.username},{"role",result.user.role}}}});
        });

        // Login
        CROW_ROUTE(app, "/auth/login").methods("POST"_method)
        ([this](const crow::request& req) {
            auto body = json::parse(req.body, nullptr, false);
            if (body.is_discarded()) return err_resp(400, "Invalid JSON");
            auto result = master.auth().loginUser(
                body.value("username",""), body.value("password",""));
            if (!result.success) return err_resp(401, result.error);
            return ok_resp({{"token",result.token},{"refresh",result.refresh_token},
                {"user",{{"id",result.user.id},{"username",result.user.username},{"role",result.user.role}}}});
        });

        // Logout
        CROW_ROUTE(app, "/auth/logout").methods("POST"_method)
        ([this](const crow::request& req) {
            auto body = json::parse(req.body, nullptr, false);
            if (!body.is_discarded())
                master.auth().logout(body.value("refresh_token",""));
            return ok_resp({{"status","ok"}});
        });

        // Server browser list
        CROW_ROUTE(app, "/servers")
        ([this] {
            auto servers = master.registry().getAll();
            json arr = json::array();
            for (auto& s : servers) {
                arr.push_back({
                    {"id",              s.id},
                    {"name",            s.name},
                    {"description",     s.description},
                    {"host",            s.host},
                    {"port",            s.port},
                    {"map",             s.map},
                    {"max_players",     s.max_players},
                    {"current_players", s.current_players},
                    {"password",        s.password_protected},
                    {"version",         s.version},
                    {"tags",            json::parse(s.tags, nullptr, false)}
                });
            }
            return ok_resp({{"servers", arr}});
        });

        // Server register
        CROW_ROUTE(app, "/servers/register").methods("POST"_method)
        ([this](const crow::request& req) {
            auto body = json::parse(req.body, nullptr, false);
            if (body.is_discarded()) return err_resp(400, "Invalid JSON");
            std::string token = body.value("auth_token","");
            if (token.empty()) token = master.auth().generateServerToken();
            GameServerEntry entry{0,
                body.value("name","Unnamed"),
                body.value("description",""),
                body.value("host",""),
                body.value("port",4444),
                body.value("map","gridmap_v2"),
                body.value("max_players",8), 0,
                body.value("password",false),
                body.value("version","1.0.0"),
                token, body.value("tags","[]"), ""};
            auto id = master.registry().registerServer(entry);
            return ok_resp({{"server_id", id},{"auth_token", token}});
        });

        // Heartbeat
        CROW_ROUTE(app, "/servers/heartbeat").methods("POST"_method)
        ([this](const crow::request& req) {
            auto body = json::parse(req.body, nullptr, false);
            if (body.is_discarded()) return err_resp(400,"Invalid JSON");
            bool ok = master.registry().heartbeat(
                body.value("auth_token",""),
                body.value("current_players",0),
                body.value("map",""));
            if (!ok) return err_resp(404,"Server not registered");
            return ok_resp({{"status","ok"}});
        });

        // Unregister
        CROW_ROUTE(app, "/servers/unregister").methods("POST"_method)
        ([this](const crow::request& req) {
            auto body = json::parse(req.body, nullptr, false);
            if (!body.is_discarded())
                master.registry().removeServer(body.value("auth_token",""));
            return ok_resp({{"status","ok"}});
        });

        // Mod list
        CROW_ROUTE(app, "/mods")([this] {
            json arr = json::array();
            master.db().query(
                "SELECT id,name,filename,version,size_bytes,sha256,description,downloads "
                "FROM mods ORDER BY downloads DESC",
                [&](sqlite3_stmt* s) {
                    arr.push_back({
                        {"id",          Database::col_int64(s,0)},
                        {"name",        Database::col_text(s,1)},
                        {"filename",    Database::col_text(s,2)},
                        {"version",     Database::col_text(s,3)},
                        {"size",        Database::col_int64(s,4)},
                        {"sha256",      Database::col_text(s,5)},
                        {"description", Database::col_text(s,6)},
                        {"downloads",   Database::col_int(s,7)}
                    });
                });
            return ok_resp({{"mods", arr}});
        });

        // Ban check
        CROW_ROUTE(app, "/bans/check/<string>")
        ([this](const std::string& identifier) {
            bool banned = false; std::string reason;
            master.db().query(
                "SELECT b.reason FROM bans b JOIN users u ON b.user_id=u.id "
                "WHERE (u.username=? OR b.ip_address=?) "
                "AND (b.expires_at IS NULL OR b.expires_at > datetime('now')) LIMIT 1",
                [&](sqlite3_stmt* s){ banned=true; reason=Database::col_text(s,0); },
                identifier, identifier);
            return ok_resp({{"banned",banned},{"reason",reason}});
        });
    }
};

RestAPI::RestAPI(MasterServer& master, const std::string& host, int port)
    : m_impl(std::make_unique<Impl>(master, host, port))
{ spdlog::info("REST API initialized on {}:{}", host, port); }

RestAPI::~RestAPI() = default;

void RestAPI::run() {
    spdlog::info("REST API listening on {}:{}", m_impl->host, m_impl->port);
    m_impl->app.bindaddr(m_impl->host).port(m_impl->port).multithreaded().run();
}

void RestAPI::stop() { m_impl->app.stop(); }

} // namespace novaMP
