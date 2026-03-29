// client/src/compat/beammp_auth.hpp
//
// BeamMPAuth — authenticates against BeamMP's auth server and caches the
// resulting private key to disk so it can be reused if the auth server is
// temporarily unreachable.
//
// Auth flow:
//   POST https://auth.beammp.com/userlogin
//     Body: { "username": "...", "password": "sha256(password)" }
//   Response: { "success": true, "message": "<private_key>" }
//
// The private key is then sent to the BeamMP game server in a CODE_AUTH_PLAYER
// ('P') packet during the handshake.
//
// Cache file: %APPDATA%/NovaMP/beammp_token.json  (Windows)
//             ~/.config/NovaMP/beammp_token.json  (Linux)
#pragma once
#include <string>
#include <functional>

namespace novaMP::beammp {

class BeamMPAuth {
public:
    using LogFn = std::function<void(const std::string&)>;

    explicit BeamMPAuth(LogFn log_fn = nullptr);

    // Attempt live auth. On failure, tries the cached token.
    // Returns true and sets private_key() if successful.
    bool authenticate(const std::string& username, const std::string& password);

    // Attempt to load a previously cached token without hitting the network.
    // Returns true if a valid (non-expired) cache entry exists.
    bool loadCached(const std::string& username);

    const std::string& privateKey()  const { return m_private_key; }
    const std::string& username()    const { return m_username; }
    bool               isLoggedIn()  const { return !m_private_key.empty(); }

    void clearCache();

private:
    std::string m_username;
    std::string m_private_key;
    LogFn       m_log;

    bool  tryLiveAuth(const std::string& username, const std::string& password);
    bool  tryCache   (const std::string& username);
    void  saveCache  (const std::string& username, const std::string& key);
    std::string cacheFilePath() const;
    std::string sha256Hex(const std::string& input) const;
    std::string httpPost(const std::string& url, const std::string& body,
                         const std::string& content_type) const;
    void log(const std::string& msg) const;
};

} // namespace novaMP::beammp
