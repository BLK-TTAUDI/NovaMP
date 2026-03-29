// client/src/auth/nova_auth.hpp
//
// NovaAuth — authenticates against the NovaMP master server and caches the
// resulting JWT token so it survives client restarts.
//
// Auth flow:
//   POST https://master.novaMP.gg/auth/login
//     Body: { "username": "...", "password": "..." }
//   Response: { "success": true, "token": "<jwt>", "expires_in": 2592000 }
//
// The token is attached to every NovaMP server connection as a
// Bearer token in the AUTH handshake packet.
//
// Cache file: %APPDATA%/NovaMP/novamp_token.json   (Windows)
//             ~/.config/NovaMP/novamp_token.json   (Linux)
#pragma once
#include <string>
#include <functional>

namespace novaMP {

class NovaAuth {
public:
    using LogFn = std::function<void(const std::string&)>;

    explicit NovaAuth(LogFn log_fn = nullptr);

    // Attempt live auth against the NovaMP master server.
    // Falls back to the cached token if the network is unreachable.
    // Returns true and populates token() / username() on success.
    bool authenticate(const std::string& username, const std::string& password);

    // Load a previously cached token without hitting the network.
    // Returns true if a valid, non-expired cache entry exists for that username.
    bool loadCached(const std::string& username);

    const std::string& token()      const { return m_token; }
    const std::string& username()   const { return m_username; }
    bool               isLoggedIn() const { return !m_token.empty(); }

    void clearCache();

private:
    std::string m_username;
    std::string m_token;
    LogFn       m_log;

    bool tryLiveAuth(const std::string& username, const std::string& password);
    bool tryCache   (const std::string& username);
    void saveCache  (const std::string& username, const std::string& token,
                     int64_t expires_in);

    std::string cacheFilePath() const;
    std::string httpPost(const std::string& url, const std::string& body,
                         const std::string& content_type) const;
    void log(const std::string& msg) const;
};

} // namespace novaMP
