// client/src/auth/nova_auth.cpp
#include "nova_auth.hpp"
#include <nlohmann/json.hpp>
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <filesystem>
#include <chrono>

#ifdef _WIN32
#  include <shlobj.h>
#endif

namespace novaMP {
namespace fs = std::filesystem;
using json   = nlohmann::json;

// Cached token is valid for up to 30 days (server issues 30-day JWTs).
static constexpr int64_t CACHE_TTL_SECS = 30LL * 24 * 3600;

NovaAuth::NovaAuth(LogFn log_fn) : m_log(std::move(log_fn)) {}

// ── Public ────────────────────────────────────────────────────────────────────

bool NovaAuth::authenticate(const std::string& username,
                             const std::string& password)
{
    m_username = username;
    m_token    = "";

    log("Authenticating with NovaMP master server...");
    if (tryLiveAuth(username, password)) {
        log("NovaMP auth successful.");
        return true;
    }

    log("Live auth failed — trying cached token...");
    if (tryCache(username)) {
        log("Using cached NovaMP token.");
        return true;
    }

    log("NovaMP auth failed: no live connection and no valid cached token.");
    return false;
}

bool NovaAuth::loadCached(const std::string& username) {
    m_username = username;
    return tryCache(username);
}

void NovaAuth::clearCache() {
    auto path = cacheFilePath();
    if (fs::exists(path)) fs::remove(path);
    m_token = "";
}

// ── Private ───────────────────────────────────────────────────────────────────

bool NovaAuth::tryLiveAuth(const std::string& username,
                            const std::string& password)
{
    json body = {{"username", username}, {"password", password}};

    std::string response;
    try {
        response = httpPost("https://master.novaMP.gg/auth/login",
                            body.dump(), "application/json");
    } catch (const std::exception& e) {
        log(std::string("Auth HTTP error: ") + e.what());
        return false;
    }

    if (response.empty()) return false;

    json j;
    try { j = json::parse(response); }
    catch (...) { log("Auth: invalid JSON response"); return false; }

    if (!j.value("success", false)) {
        log("Auth rejected: " + j.value("message", "unknown error"));
        return false;
    }

    m_token = j.value("token", "");
    if (m_token.empty()) return false;

    int64_t expires_in = j.value("expires_in", CACHE_TTL_SECS);
    saveCache(username, m_token, expires_in);
    return true;
}

bool NovaAuth::tryCache(const std::string& username) {
    std::ifstream f(cacheFilePath());
    if (!f.is_open()) return false;

    json j;
    try { j = json::parse(f); }
    catch (...) { return false; }

    if (j.value("username", "") != username) return false;

    int64_t saved_at   = j.value("saved_at",   (int64_t)0);
    int64_t expires_in = j.value("expires_in", CACHE_TTL_SECS);
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (now - saved_at > expires_in) {
        log("Cached NovaMP token has expired.");
        return false;
    }

    m_token = j.value("token", "");
    return !m_token.empty();
}

void NovaAuth::saveCache(const std::string& username, const std::string& token,
                          int64_t expires_in)
{
    auto path = cacheFilePath();
    fs::create_directories(path.parent_path());

    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    json j = {
        {"username",   username},
        {"token",      token},
        {"saved_at",   now},
        {"expires_in", expires_in}
    };
    std::ofstream out(path);
    if (out) out << j.dump(2);
}

std::string NovaAuth::cacheFilePath() const {
#ifdef _WIN32
    char buf[MAX_PATH];
    SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, buf);
    return std::string(buf) + "\\NovaMP\\novamp_token.json";
#else
    const char* home = std::getenv("HOME");
    std::string base = home ? std::string(home) : "/tmp";
    return base + "/.config/NovaMP/novamp_token.json";
#endif
}

// Minimal HTTPS POST using ASIO + OpenSSL
std::string NovaAuth::httpPost(const std::string& url,
                                const std::string& body,
                                const std::string& content_type) const
{
    std::string host, path;
    auto after = url.find("://");
    std::string rest = (after != std::string::npos) ? url.substr(after + 3) : url;
    auto slash = rest.find('/');
    if (slash != std::string::npos) {
        host = rest.substr(0, slash);
        path = rest.substr(slash);
    } else {
        host = rest; path = "/";
    }

    asio::io_context ioc;
    asio::ssl::context ssl_ctx(asio::ssl::context::tls_client);
    ssl_ctx.set_default_verify_paths();
    ssl_ctx.set_verify_mode(asio::ssl::verify_peer);

    asio::ssl::stream<asio::ip::tcp::socket> sock(ioc, ssl_ctx);
    sock.set_verify_callback(asio::ssl::host_name_verification(host));

    asio::ip::tcp::resolver resolver(ioc);
    asio::connect(sock.lowest_layer(), resolver.resolve(host, "443"));
    sock.handshake(asio::ssl::stream_base::client);

    std::string req =
        "POST " + path + " HTTP/1.0\r\n"
        "Host: " + host + "\r\n"
        "Content-Type: " + content_type + "\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n\r\n" + body;
    asio::write(sock, asio::buffer(req));

    std::string response;
    std::error_code ec;
    char buf[4096];
    while (true) {
        size_t n = sock.read_some(asio::buffer(buf), ec);
        if (n > 0) response.append(buf, n);
        if (ec) break;
    }

    auto pos = response.find("\r\n\r\n");
    return pos != std::string::npos ? response.substr(pos + 4) : response;
}

void NovaAuth::log(const std::string& msg) const {
    if (m_log) m_log(msg);
    else spdlog::info("[NovaAuth] {}", msg);
}

} // namespace novaMP
