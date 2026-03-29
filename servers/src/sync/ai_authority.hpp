// servers/src/sync/ai_authority.hpp
//
// AIAuthority — manages which source provides AI vehicle states.
//
// Priority order (when mode = "auto"):
//   1. Headless BeamNG.drive instance launched locally (full engine AI)
//   2. Connected client that volunteered via AUTHORITY_CLAIM
//   3. Built-in C++ waypoint-following AI (fallback)
//
// When a headless BeamNG instance or a client is the authority it sends
// ordinary VEHICLE_UPDATE (0x08) packets with VF_IS_AI set, which the
// GameServer forwards to all other clients exactly like player vehicles.
// The built-in AITraffic class is only ticked when in BUILTIN mode.

#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <cstdint>

namespace novaMP {

struct ServerConfig;

enum class AIAuthorityMode {
    NONE,       // AI disabled entirely
    HEADLESS,   // A local headless BeamNG instance is the authority
    CLIENT,     // A connected regular client is the authority
    BUILTIN,    // Built-in C++ waypoint AI (AITraffic)
};

class AIAuthority {
public:
    // Called by GameServer when a source needs to notify the authority manager.
    using GrantFn  = std::function<void(uint16_t player_id)>;  // tells client it's granted
    using RevokeFn = std::function<void(uint16_t player_id)>;  // tells client it's revoked
    using FallbackFn = std::function<void()>;                  // activate builtin AI

    AIAuthority(const ServerConfig& cfg,
                GrantFn grant_fn, RevokeFn revoke_fn, FallbackFn fallback_fn);

    // Called from GameServer::run() in its own thread — blocks until stopped.
    void negotiationLoop(std::atomic<bool>& running);

    // Called when a client sends AUTHORITY_CLAIM.
    // Returns true if the claim is accepted (i.e. we were waiting for one).
    bool onClientClaim(uint16_t player_id);

    // Called when the current authority disconnects.
    void onAuthorityDisconnect(uint16_t player_id);

    // Called when the headless BeamNG bridge connects (special token auth).
    void onHeadlessConnect(uint16_t session_player_id);

    // True if AI states arriving from player_id should be rebroadcast as AI
    // (i.e. that player is the current authority).
    bool isAuthority(uint16_t player_id) const;

    AIAuthorityMode mode()       const { return m_mode; }
    uint16_t        authorityID()const { return m_authority_player_id; }

private:
    const ServerConfig& m_cfg;
    GrantFn    m_grant;
    RevokeFn   m_revoke;
    FallbackFn m_fallback;

    mutable std::mutex    m_mutex;
    AIAuthorityMode       m_mode{AIAuthorityMode::NONE};
    uint16_t              m_authority_player_id{0xFFFF};

    // Set once we know we have a headless session
    bool                  m_headless_connected{false};

    // Timestamp when we started waiting for headless/client
    std::chrono::steady_clock::time_point m_wait_start;
    bool                  m_waiting{false};

    void setMode(AIAuthorityMode m, uint16_t pid = 0xFFFF);
};

} // namespace novaMP
