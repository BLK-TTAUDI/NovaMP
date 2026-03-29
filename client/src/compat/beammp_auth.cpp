// client/src/compat/beammp_auth.cpp
#include "beammp_auth.hpp"
#include <nlohmann/json.hpp>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <chrono>

#ifdef _WIN32
#  include <shlobj.h>
#endif

namespace novaMP::beammp {
namespace fs = std::filesystem;
using json   = nlohmann::json;

BeamMPAuth::BeamMPAuth(LogFn log_fn) : m_log(std::move(log_fn)) {}

// ── Public ────────────────────────────────────────────────────────────────────

bool BeamMPAuth::authenticate(const std::string& username,
                               const std::string& password)
{
    m_username    = username;
    m_private_key = "";

    log("Authenticating with BeamMP auth server...");
    if (tryLiveAuth(username, password)) {
        log("BeamMP auth successful.");
        saveCache(username, m_private_key);
        return true;
    }

    log("Live auth failed — trying cached token...");
    if (tryCache(username)) {
        log("Using cached BeamMP token (offline/fallback mode).");
        return true;
    }

    log("BeamMP auth failed: no live connection and no cached token.");
    return false;
}

bool BeamMPAuth::loadCached(const std::string& username) {
    m_username = username;
    return tryCache(username);
}

void BeamMPAuth::clearCache() {
    auto path = cacheFilePath();
    if (fs::exists(path)) fs::remove(path);
    m_private_key = "";
}

// ── Private ───────────────────────────────────────────────────────────────────

bool BeamMPAuth::tryLiveAuth(const std::string& username,
                              const std::string& password)
{
    // BeamMP auth endpoint — POST with JSON body containing hashed password
    // Ref: BeamMP-Launcher (AGPL-3.0) auth flow
    json body = {
        {"username", username},
        {"password", sha256Hex(password)}
    };

    std::string response;
    try {
        response = httpPost("https://auth.beammp.com/userlogin",
                            body.dump(), "application/json");
    } catch (const std::exception& e) {
        log(std::string("Auth HTTP error: ") + e.what());
        return false;
    }

    if (response.empty()) return false;

    json j;
    try { j = json::parse(response); }
    catch (...) { log("Auth: invalid JSON response"); return false; }

    bool success = j.value("success", false);
    if (!success) {
        log("Auth rejected: " + j.value("message", "unknown error"));
        return false;
    }

    m_private_key = j.value("message", "");
    return !m_private_key.empty();
}

bool BeamMPAuth::tryCache(const std::string& username) {
    auto path = cacheFilePath();
    std::ifstream f(path);
    if (!f.is_open()) return false;

    json j;
    try { j = json::parse(f); }
    catch (...) { return false; }

    if (j.value("username", "") != username) return false;

    // Check expiry (cache valid for 7 days)
    int64_t saved_at = j.value("saved_at", (int64_t)0);
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    constexpr int64_t CACHE_TTL = 7 * 24 * 3600;
    if (now - saved_at > CACHE_TTL) {
        log("Cached BeamMP token expired.");
        return false;
    }

    m_private_key = j.value("key", "");
    return !m_private_key.empty();
}

void BeamMPAuth::saveCache(const std::string& username, const std::string& key) {
    auto path = cacheFilePath();
    fs::create_directories(path.parent_path());

    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    json j = {{"username", username}, {"key", key}, {"saved_at", now}};
    std::ofstream f(path);
    if (f) f << j.dump(2);
}

std::string BeamMPAuth::cacheFilePath() const {
#ifdef _WIN32
    char buf[MAX_PATH];
    SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, buf);
    return std::string(buf) + "\\NovaMP\\beammp_token.json";
#else
    const char* home = std::getenv("HOME");
    std::string base = home ? std::string(home) : "/tmp";
    return base + "/.config/NovaMP/beammp_token.json";
#endif
}

std::string BeamMPAuth::sha256Hex(const std::string& input) const {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()),
           input.size(), hash);
    std::ostringstream oss;
    for (auto b : hash)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    return oss.str();
}

// Minimal HTTPS POST using ASIO + OpenSSL (no libcurl dependency)
std::string BeamMPAuth::httpPost(const std::string& url,
                                  const std::string& body,
                                  const std::string& content_type) const
{
    // Parse URL: must be https://host/path
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
    auto endpoints = resolver.resolve(host, "443");
    asio::connect(sock.lowest_layer(), endpoints);
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

    auto body_start = response.find("\r\n\r\n");
    return body_start != std::string::npos
           ? response.substr(body_start + 4) : response;
}

void BeamMPAuth::log(const std::string& msg) const {
    if (m_log) m_log(msg);
    else spdlog::info("[BeamMPAuth] {}", msg);
}

} // namespace novaMP::beammp
