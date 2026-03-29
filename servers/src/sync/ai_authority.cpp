// servers/src/sync/ai_authority.cpp
#include "ai_authority.hpp"
#include "../config/config.hpp"
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

namespace novaMP {
using namespace std::chrono_literals;

AIAuthority::AIAuthority(const ServerConfig& cfg,
                         GrantFn grant_fn, RevokeFn revoke_fn, FallbackFn fallback_fn)
    : m_cfg(cfg)
    , m_grant(std::move(grant_fn))
    , m_revoke(std::move(revoke_fn))
    , m_fallback(std::move(fallback_fn))
{}

void AIAuthority::negotiationLoop(std::atomic<bool>& running) {
    // "builtin" or disabled → nothing to negotiate
    if (!m_cfg.ai_enabled || m_cfg.authority_mode == "builtin") {
        if (m_cfg.ai_enabled) {
            setMode(AIAuthorityMode::BUILTIN);
            m_fallback();
        }
        return;
    }

    auto timeout = std::chrono::seconds(m_cfg.authority_timeout_sec);
    bool try_headless = (m_cfg.authority_mode == "auto" || m_cfg.authority_mode == "headless");
    bool try_client   = (m_cfg.authority_mode == "auto" || m_cfg.authority_mode == "client");

    // ── Phase 1: wait for headless BeamNG to connect ─────────────────────────
    if (try_headless) {
        spdlog::info("[AIAuthority] Waiting up to {}s for headless BeamNG...",
                     m_cfg.authority_timeout_sec);
        {
            std::lock_guard lk(m_mutex);
            m_waiting    = true;
            m_wait_start = std::chrono::steady_clock::now();
        }

        while (running) {
            {
                std::lock_guard lk(m_mutex);
                if (m_headless_connected) {
                    spdlog::info("[AIAuthority] Headless BeamNG is now the AI authority.");
                    m_waiting = false;
                    return;  // HEADLESS mode set in onHeadlessConnect
                }
                if (std::chrono::steady_clock::now() - m_wait_start >= timeout) {
                    m_waiting = false;
                    break;
                }
            }
            std::this_thread::sleep_for(200ms);
        }

        if (!running) return;

        if (m_cfg.authority_mode == "headless") {
            spdlog::warn("[AIAuthority] Headless BeamNG not available. AI disabled.");
            setMode(AIAuthorityMode::NONE);
            return;
        }

        spdlog::info("[AIAuthority] No headless BeamNG. Trying client volunteer...");
    }

    // ── Phase 2: wait for a client to send AUTHORITY_CLAIM ───────────────────
    if (try_client) {
        {
            std::lock_guard lk(m_mutex);
            m_waiting    = true;
            m_wait_start = std::chrono::steady_clock::now();
        }

        spdlog::info("[AIAuthority] Waiting up to {}s for a client AI authority volunteer...",
                     m_cfg.authority_timeout_sec);

        while (running) {
            {
                std::lock_guard lk(m_mutex);
                if (m_mode == AIAuthorityMode::CLIENT) {
                    m_waiting = false;
                    return;  // CLIENT mode set in onClientClaim
                }
                if (std::chrono::steady_clock::now() - m_wait_start >= timeout) {
                    m_waiting = false;
                    break;
                }
            }
            std::this_thread::sleep_for(200ms);
        }

        if (!running) return;

        if (m_cfg.authority_mode == "client") {
            spdlog::warn("[AIAuthority] No client volunteer. AI disabled.");
            setMode(AIAuthorityMode::NONE);
            return;
        }
    }

    // ── Phase 3: fall back to built-in C++ AI ────────────────────────────────
    spdlog::info("[AIAuthority] Falling back to built-in C++ waypoint AI.");
    setMode(AIAuthorityMode::BUILTIN);
    m_fallback();
}

bool AIAuthority::onClientClaim(uint16_t player_id) {
    std::lock_guard lk(m_mutex);

    // Only accept if we are actively waiting for a volunteer (phase 2).
    if (!m_waiting) {
        spdlog::debug("[AIAuthority] Client {} claimed authority but we are not waiting.", player_id);
        return false;
    }
    // Don't steal from headless
    if (m_mode == AIAuthorityMode::HEADLESS) return false;

    setMode(AIAuthorityMode::CLIENT, player_id);
    m_grant(player_id);
    spdlog::info("[AIAuthority] Client {} granted AI authority.", player_id);
    return true;
}

void AIAuthority::onAuthorityDisconnect(uint16_t player_id) {
    std::lock_guard lk(m_mutex);
    if (m_authority_player_id != player_id) return;

    spdlog::warn("[AIAuthority] Authority source (player {}) disconnected.", player_id);

    if (m_mode == AIAuthorityMode::HEADLESS) {
        m_headless_connected = false;
    }

    // If we are in "headless" or "client" exclusive mode → disable AI.
    // In "auto" mode → immediately fall back to built-in.
    if (m_cfg.authority_mode == "auto") {
        spdlog::info("[AIAuthority] Falling back to built-in C++ AI after authority loss.");
        setMode(AIAuthorityMode::BUILTIN);
        m_fallback();
    } else {
        spdlog::warn("[AIAuthority] AI disabled (authority lost, mode={})", m_cfg.authority_mode);
        setMode(AIAuthorityMode::NONE);
    }
}

void AIAuthority::onHeadlessConnect(uint16_t session_player_id) {
    std::lock_guard lk(m_mutex);
    m_headless_connected = true;
    setMode(AIAuthorityMode::HEADLESS, session_player_id);
    // No AUTHORITY_GRANT packet sent — the bridge already knows it's the authority.
    spdlog::info("[AIAuthority] Headless BeamNG bridge authenticated (pid={}).",
                 session_player_id);
}

bool AIAuthority::isAuthority(uint16_t player_id) const {
    std::lock_guard lk(m_mutex);
    return (m_mode == AIAuthorityMode::HEADLESS || m_mode == AIAuthorityMode::CLIENT)
        && m_authority_player_id == player_id;
}

void AIAuthority::setMode(AIAuthorityMode m, uint16_t pid) {
    // Caller holds m_mutex
    m_mode                = m;
    m_authority_player_id = pid;
}

} // namespace novaMP
