// servers/src/game_server.cpp
#include "game_server.hpp"
#include "network/packet.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cmath>
#include <chrono>
#include <thread>
#include <algorithm>

namespace novaMP {
using json = nlohmann::json;
using namespace std::chrono_literals;

GameServer::GameServer(const ServerConfig& cfg) : m_cfg(cfg) {
    // Logging
    auto cs = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto fs = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        cfg.log_file, 10*1024*1024, 3);
    spdlog::set_default_logger(
        std::make_shared<spdlog::logger>("novaMP", spdlog::sinks_init_list{cs, fs}));
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    spdlog::info("NovaMP Dedicated Server v1.0.0");
    spdlog::info("Map: {}  MaxPlayers: {}  AI: {}",
                 cfg.map, cfg.max_players, cfg.ai_enabled ? "ON" : "OFF");

    // Vehicle sync — relay to all except owner
    m_vsync = std::make_unique<VehicleSync>(cfg.vehicle_sync_hz,
        [this](const std::vector<uint8_t>& pkt, uint16_t exclude) {
            m_udp->sendAll(pkt, exclude);
        });

    // AI traffic (built-in fallback) — relay to all
    m_ai = std::make_unique<AITraffic>(cfg,
        [this](const std::vector<uint8_t>& pkt) { m_udp->sendAll(pkt); });

    if (cfg.ai_enabled) m_ai->loadRoadNetwork(cfg.map);

    // AI authority manager — decides which source drives AI vehicles
    if (cfg.ai_enabled) {
        m_authority = std::make_unique<AIAuthority>(cfg,
            [this](uint16_t pid) { grantAuthority(pid);  },
            [this](uint16_t pid) { revokeAuthority(pid); },
            [this]()             {
                // Fallback: start the built-in AI traffic loop
                spdlog::info("[Authority] Starting built-in C++ AI.");
                // aiTrafficLoop is already running if ai_enabled; nothing more to do.
            });
    }

    // Headless launcher (used when authority_mode != "builtin" and != "client")
    if (cfg.ai_enabled &&
        cfg.authority_mode != "builtin" && cfg.authority_mode != "client")
    {
        m_headless = std::make_unique<HeadlessLauncher>(cfg);
    }

    // Lua plugins
    m_lua = std::make_unique<LuaEngine>(*this, cfg.resources_dir);
    m_lua->loadPlugins();

    // Console
    m_console = std::make_unique<Console>(*this);

    // Network (must be after Lua so callbacks are ready)
    m_udp = std::make_unique<UDPServer>(m_ioc, (uint16_t)cfg.port,
        [this](const UDPEndpoint& from, const uint8_t* d, size_t n) {
            onUDPPacket(from, d, n);
        });

    m_tcp = std::make_unique<TCPServer>(m_ioc, (uint16_t)cfg.port,
        [this](uint16_t sid, const uint8_t* d, size_t n) {
            onTCPPacket(sid, d, n);
        },
        [this](uint16_t sid) { onDisconnect(sid); });

    if (cfg.rcon_enabled)
        m_rcon = std::make_unique<RconServer>(*this, m_ioc,
                     (uint16_t)cfg.rcon_port, cfg.rcon_password);
}

GameServer::~GameServer() { stop(); }

void GameServer::run() {
    m_running = true;

    m_threads.emplace_back([this] { vehicleSyncLoop(); });
    if (m_cfg.ai_enabled)
        m_threads.emplace_back([this] { aiTrafficLoop(); });
    if (m_cfg.master_register)
        m_threads.emplace_back([this] { heartbeatLoop(); });
    if (m_cfg.ai_enabled && m_authority) {
        // Write bridge config so headless BeamNG knows where to connect
        writeBridgeConfig();
        // Try to launch headless BeamNG before the negotiation loop starts
        if (m_headless) {
            m_headless->launch([](const std::string& s){ spdlog::info("[Headless] {}", s); });
        }
        m_threads.emplace_back([this] { authorityNegotiationLoop(); });
    }
    m_threads.emplace_back([this] { m_console->run(); });

    unsigned hw = std::max(2u, std::thread::hardware_concurrency());
    for (unsigned i = 0; i < hw; ++i)
        m_threads.emplace_back([this] { m_ioc.run(); });

    spdlog::info("Server running on port {}", m_cfg.port);
    m_lua->fireEvent("onInit");

    for (auto& t : m_threads)
        if (t.joinable()) t.join();
}

void GameServer::stop() {
    if (!m_running.exchange(false)) return;
    spdlog::info("Server stopping...");
    m_lua->fireEvent("onShutdown");
    if (m_headless) m_headless->terminate();
    m_ioc.stop();
}

// ── UDP ───────────────────────────────────────────────────────────────────────
void GameServer::onUDPPacket(const UDPEndpoint& from,
                              const uint8_t* data, size_t len)
{
    auto hdr  = Packet::parseHeader(data, len);
    auto type = static_cast<PacketType>(hdr.type);

    if (type == PacketType::PING) {
        auto pong = Packet::build(PacketType::PONG, 0, hdr.sequence, nullptr, 0);
        m_udp->send(from, pong);
        return;
    }

    uint16_t pid = m_udp->playerFromEndpoint(from);
    if (pid == 0xFFFF) return;

    if (type == PacketType::VEHICLE_UPDATE) {
        // If this player is the AI authority, their VF_IS_AI packets are
        // rebroadcast to all OTHER clients unchanged (not put in vehicle sync).
        if (m_authority && m_authority->isAuthority(pid) && hdr.payload_len >= 2) {
            uint8_t vflags = Packet::payload(data)[1];
            if (vflags & VF_IS_AI) {
                // Rebroadcast raw to everyone except the authority sender
                std::vector<uint8_t> raw(data, data + hdr.payload_len + Packet::HEADER_SIZE);
                m_udp->sendAll(raw, pid);
                return;
            }
        }
        handleVehicleUpdate(pid, Packet::payload(data), hdr.payload_len);
    }
}

// ── TCP ───────────────────────────────────────────────────────────────────────
void GameServer::onTCPPacket(uint16_t session_id,
                              const uint8_t* data, size_t len)
{
    auto hdr  = Packet::parseHeader(data, len);
    auto type = static_cast<PacketType>(hdr.type);
    auto* pl  = Packet::payload(data);
    uint16_t plen = hdr.payload_len;

    uint16_t pid = 0xFFFF;
    {
        std::lock_guard lk(m_players_mutex);
        auto it = m_session_to_player.find(session_id);
        if (it != m_session_to_player.end()) pid = it->second;
    }

    switch (type) {
    case PacketType::AUTH_REQUEST:     handleAuth(session_id, pl, plen);     break;
    case PacketType::VEHICLE_SPAWN:    if (pid!=0xFFFF) handleVehicleSpawn(pid,pl,plen); break;
    case PacketType::VEHICLE_DELETE:   if (pid!=0xFFFF) handleVehicleDelete(pid,pl,plen); break;
    case PacketType::CHAT_MESSAGE:     if (pid!=0xFFFF) handleChat(pid,pl,plen); break;
    case PacketType::READY:            if (pid!=0xFFFF) handleReady(pid);    break;
    case PacketType::AUTHORITY_CLAIM:  if (pid!=0xFFFF) handleAuthorityClaim(pid); break;
    default: break;
    }
}

void GameServer::onDisconnect(uint16_t session_id) {
    uint16_t pid = 0xFFFF;
    {
        std::lock_guard lk(m_players_mutex);
        auto it = m_session_to_player.find(session_id);
        if (it != m_session_to_player.end()) {
            pid = it->second;
            m_session_to_player.erase(it);
        }
    }
    if (pid == 0xFFFF) return;

    std::string username;
    {
        std::lock_guard lk(m_players_mutex);
        auto it = m_players.find(pid);
        if (it != m_players.end()) {
            username = it->second->username;
            m_vsync->onPlayerDisconnect(pid);
            m_udp->unmapPlayer(pid);
            m_players.erase(it);
        }
    }
    broadcastPlayerLeave(pid, "disconnected");
    m_lua->fireEvent("onPlayerDisconnect", {std::to_string(pid)});
    if (m_authority) m_authority->onAuthorityDisconnect(pid);
    spdlog::info("Player {} ({}) disconnected. Players: {}/{}",
                 username, pid, playerCount(), m_cfg.max_players);
}

// ── Auth ──────────────────────────────────────────────────────────────────────
void GameServer::handleAuth(uint16_t session_id,
                             const uint8_t* payload, uint16_t len)
{
    std::string body(reinterpret_cast<const char*>(payload), len);
    json j;
    try { j = json::parse(body); }
    catch (...) {
        m_tcp->send(session_id,
            Packet::buildStr(PacketType::AUTH_RESPONSE, 0, 0,
                json{{"ok",false},{"error","Invalid JSON"}}.dump()));
        return;
    }

    std::string username = j.value("username", "");
    std::string password = j.value("server_password", "");

    // ── Headless AI bridge authentication ─────────────────────────────────────
    // The bridge mod identifies itself with a special username and the
    // bridge_token.  It bypasses normal player limits & password checks.
    if (username == "##ai_bridge##") {
        if (!m_authority || password != m_cfg.bridge_token) {
            m_tcp->send(session_id,
                Packet::buildStr(PacketType::AUTH_RESPONSE, 0, 0,
                    json{{"ok",false},{"error","Bridge token mismatch"}}.dump()));
            m_tcp->disconnect(session_id);
            return;
        }
        uint16_t pid = allocPlayerID();
        auto player          = std::make_unique<Player>();
        player->id           = pid;
        player->username     = "##ai_bridge##";
        player->role         = "bridge";
        player->ip           = m_tcp->getRemoteIP(session_id);
        player->state        = PlayerState::READY;
        player->connect_time = std::chrono::steady_clock::now();
        player->last_packet_time = player->connect_time;
        {
            std::lock_guard lk(m_players_mutex);
            m_session_to_player[session_id] = pid;
            m_players[pid] = std::move(player);
        }
        m_tcp->send(session_id,
            Packet::buildStr(PacketType::AUTH_RESPONSE, 0, 0,
                json{{"ok",true},{"player_id",pid},{"map",m_cfg.map}}.dump()));
        m_authority->onHeadlessConnect(pid);
        spdlog::info("Headless AI bridge connected (pid={}).", pid);
        return;
    }

    if (username.empty()) {
        m_tcp->send(session_id,
            Packet::buildStr(PacketType::AUTH_RESPONSE, 0, 0,
                json{{"ok",false},{"error","Username required"}}.dump()));
        return;
    }
    if (!m_cfg.password.empty() && password != m_cfg.password) {
        m_tcp->send(session_id,
            Packet::buildStr(PacketType::AUTH_RESPONSE, 0, 0,
                json{{"ok",false},{"error","Wrong password"}}.dump()));
        m_tcp->disconnect(session_id);
        return;
    }
    if (playerCount() >= m_cfg.max_players) {
        m_tcp->send(session_id,
            Packet::buildStr(PacketType::AUTH_RESPONSE, 0, 0,
                json{{"ok",false},{"error","Server full"}}.dump()));
        m_tcp->disconnect(session_id);
        return;
    }

    uint16_t pid = allocPlayerID();
    auto player           = std::make_unique<Player>();
    player->id            = pid;
    player->username      = username;
    player->role          = j.value("role", "player");
    player->ip            = m_tcp->getRemoteIP(session_id);
    player->state         = PlayerState::LOADING_MODS;
    player->connect_time  = std::chrono::steady_clock::now();
    player->last_packet_time = player->connect_time;

    {
        std::lock_guard lk(m_players_mutex);
        m_session_to_player[session_id] = pid;
        m_players[pid] = std::move(player);
    }

    // Lua veto
    auto veto = m_lua->fireEvent("onPlayerAuth",
                                 {std::to_string(pid), username});
    if (!veto.empty() && veto != "nil") {
        {
            std::lock_guard lk(m_players_mutex);
            m_players.erase(pid);
            m_session_to_player.erase(session_id);
        }
        m_tcp->send(session_id,
            Packet::buildStr(PacketType::AUTH_RESPONSE, 0, 0,
                json{{"ok",false},{"error",veto}}.dump()));
        m_tcp->disconnect(session_id);
        return;
    }

    m_tcp->send(session_id,
        Packet::buildStr(PacketType::AUTH_RESPONSE, 0, 0,
            json{{"ok",true},{"player_id",pid},{"map",m_cfg.map}}.dump()));

    sendServerInfo(session_id);

    spdlog::info("Player {} ({}) authenticated. Players: {}/{}",
                 username, pid, playerCount(), m_cfg.max_players);
}

void GameServer::handleReady(uint16_t pid) {
    Player* p = getPlayer(pid);
    if (!p) return;
    p->state = PlayerState::READY;
    broadcastPlayerJoin(*p);
    m_lua->fireEvent("onPlayerJoin", {std::to_string(pid), p->username});
    sendChat(pid, "Welcome to " + m_cfg.name + "!");
    spdlog::info("Player {} is in-game.", p->username);
}

void GameServer::handleVehicleSpawn(uint16_t pid,
                                     const uint8_t* payload, uint16_t len)
{
    std::string body(reinterpret_cast<const char*>(payload), len);
    json j;
    try { j = json::parse(body); } catch (...) { return; }

    Player* p = getPlayer(pid);
    if (!p || (int)p->vehicles.size() >= m_cfg.vehicle_limit_per_player) return;

    uint8_t vid = (uint8_t)p->vehicles.size();
    PlayerVehicle pv;
    pv.vehicle_id = vid;
    pv.model      = j.value("model", "etk800");
    pv.config     = j.dump();
    pv.spawned    = true;
    p->vehicles.push_back(pv);

    json notify = {{"type","vehicle_spawn"},{"player_id",pid},
                   {"vehicle_id",vid},{"model",pv.model},{"config",pv.config}};
    m_tcp->sendAll(Packet::buildStr(PacketType::VEHICLE_SPAWN, pid, 0, notify.dump()));
    m_lua->fireEvent("onVehicleSpawn", {std::to_string(pid), pv.model});
}

void GameServer::handleVehicleDelete(uint16_t pid,
                                      const uint8_t* payload, uint16_t len)
{
    if (len < 1) return;
    uint8_t vid = payload[0];
    Player* p   = getPlayer(pid);
    if (!p) return;

    m_vsync->onVehicleDelete(pid, vid);
    p->vehicles.erase(
        std::remove_if(p->vehicles.begin(), p->vehicles.end(),
            [vid](const PlayerVehicle& v) { return v.vehicle_id == vid; }),
        p->vehicles.end());

    m_tcp->sendAll(Packet::build(PacketType::VEHICLE_DELETE, pid, 0, &vid, 1));
    m_lua->fireEvent("onVehicleDelete", {std::to_string(pid), std::to_string(vid)});
}

void GameServer::handleVehicleUpdate(uint16_t pid,
                                      const uint8_t* payload, uint16_t len)
{
    if (len < sizeof(VehicleState)) return;
    auto vs = Packet::parseVehicleState(payload, len);
    m_vsync->onVehicleUpdate(pid, vs);
}

void GameServer::handleChat(uint16_t pid, const uint8_t* payload, uint16_t len) {
    std::string msg(reinterpret_cast<const char*>(payload), len);
    Player* p = getPlayer(pid);
    if (!p || p->is_muted) return;
    if (msg.size() > 256) msg.resize(256);

    auto blocked = m_lua->fireEvent("onChatMessage",
                                    {std::to_string(pid), p->username, msg});
    if (!blocked.empty() && blocked != "nil") return;

    std::string full = p->username + ": " + msg;
    spdlog::info("[Chat] {}", full);
    sendChat(-1, full);
}

// ── Helpers ───────────────────────────────────────────────────────────────────
void GameServer::sendChat(int player_id, const std::string& msg) {
    auto pkt = Packet::buildStr(PacketType::CHAT_MESSAGE, 0, 0, msg);
    if (player_id < 0) {
        m_tcp->sendAll(pkt);
    } else {
        std::lock_guard lk(m_players_mutex);
        for (auto& [sid, pid] : m_session_to_player)
            if (pid == (uint16_t)player_id) { m_tcp->send(sid, pkt); break; }
    }
}

void GameServer::broadcastPlayerJoin(const Player& p) {
    json j = {{"type","player_join"},{"id",p.id},{"name",p.username},{"role",p.role}};
    m_tcp->sendAll(Packet::buildStr(PacketType::PLAYER_JOIN, p.id, 0, j.dump()));
}

void GameServer::broadcastPlayerLeave(uint16_t pid, const std::string& reason) {
    json j = {{"type","player_leave"},{"id",pid},{"reason",reason}};
    m_tcp->sendAll(Packet::buildStr(PacketType::PLAYER_LEAVE, pid, 0, j.dump()));
}

void GameServer::sendServerInfo(uint16_t session_id) {
    json j = {{"name",m_cfg.name},{"description",m_cfg.description},
              {"map",m_cfg.map},{"max_players",m_cfg.max_players},
              {"current",playerCount()},{"ai_enabled",m_cfg.ai_enabled},
              {"version","1.0.0"}};
    m_tcp->send(session_id, Packet::buildStr(PacketType::SERVER_INFO, 0, 0, j.dump()));
}

void GameServer::sendExistingPlayers(uint16_t session_id) {
    std::lock_guard lk(m_players_mutex);
    for (auto& [pid, p] : m_players) {
        json j = {{"type","player_join"},{"id",pid},
                  {"name",p->username},{"role",p->role}};
        m_tcp->send(session_id,
            Packet::buildStr(PacketType::PLAYER_INFO, pid, 0, j.dump()));
    }
}

// ── Loops ─────────────────────────────────────────────────────────────────────
void GameServer::vehicleSyncLoop() {
    using clock = std::chrono::steady_clock;
    auto iv   = std::chrono::microseconds(1000000 / m_cfg.vehicle_sync_hz);
    auto next = clock::now() + iv;
    while (m_running) {
        m_vsync->broadcastAll();
        std::this_thread::sleep_until(next);
        next += iv;
    }
}

void GameServer::aiTrafficLoop() {
    using clock = std::chrono::steady_clock;
    int  hz   = std::max(1, m_cfg.ai_update_rate_hz);
    auto iv   = std::chrono::microseconds(1000000 / hz);
    float dt  = 1.0f / hz;
    auto next = clock::now() + iv;
    while (m_running) {
        std::vector<novaMP::PlayerInfo> player_info;
        {
            std::lock_guard lk(m_players_mutex);
            for (auto& [pid, p] : m_players) {
                // Skip the AI bridge pseudo-player
                if (p->username == "##ai_bridge##") continue;
                if (p->vehicles.empty()) continue;
                auto& vs = p->vehicles[0].last_state;
                novaMP::PlayerInfo info;
                info.pos[0] = vs.pos[0];
                info.pos[1] = vs.pos[1];
                info.pos[2] = vs.pos[2];
                info.vel[0] = vs.vel[0];
                info.vel[1] = vs.vel[1];
                info.vel[2] = vs.vel[2];
                info.speed  = std::sqrt(vs.vel[0]*vs.vel[0] +
                                        vs.vel[1]*vs.vel[1] +
                                        vs.vel[2]*vs.vel[2]);
                // Derive heading from velocity; fall back to 0 if stationary
                if (info.speed > 0.2f)
                    info.heading = std::atan2(vs.vel[1], vs.vel[0]);
                else
                    info.heading = 0.0f;
                player_info.push_back(info);
            }
        }
        m_ai->setPlayerInfo(player_info);
        m_ai->tick(dt);
        m_ai->broadcastAll();
        std::this_thread::sleep_until(next);
        next += iv;
    }
}

void GameServer::heartbeatLoop() {
    int interval_ms = (int)(1000.0 / std::max(m_cfg.master_heartbeat_hz, 0.001));
    masterHeartbeat();
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        if (!m_running) break;
        masterHeartbeat();
    }
}

void GameServer::masterHeartbeat() {
    spdlog::debug("Heartbeat -> master (players={})", playerCount());
    // Full HTTP POST to m_cfg.master_url + "/servers/heartbeat" goes here.
}

// ── Player management ─────────────────────────────────────────────────────────
Player* GameServer::getPlayer(uint16_t id) {
    std::lock_guard lk(m_players_mutex);
    auto it = m_players.find(id);
    return it != m_players.end() ? it->second.get() : nullptr;
}

int GameServer::playerCount() const {
    std::lock_guard lk(m_players_mutex);
    return (int)m_players.size();
}

const std::unordered_map<uint16_t, std::unique_ptr<Player>>&
GameServer::players() const { return m_players; }

void GameServer::kickPlayer(uint16_t id, const std::string& reason) {
    auto pkt = Packet::buildStr(PacketType::KICK, 0, 0, "Kicked: " + reason);
    std::lock_guard lk(m_players_mutex);
    for (auto& [sid, pid] : m_session_to_player)
        if (pid == id) { m_tcp->send(sid, pkt); m_tcp->disconnect(sid); break; }
    spdlog::info("Kicked player {} ({})", id, reason);
}

void GameServer::banPlayer(uint16_t id, const std::string& reason) {
    Player* p = getPlayer(id);
    if (p) { p->is_banned = true;
             spdlog::warn("Banned {} ({}): {}", p->username, p->ip, reason); }
    kickPlayer(id, "Banned: " + reason);
}

uint16_t GameServer::allocPlayerID() {
    if (m_next_player_id == 0xFFFF) m_next_player_id = 1;
    return m_next_player_id++;
}

// ── AI Authority ──────────────────────────────────────────────────────────────

void GameServer::authorityNegotiationLoop() {
    m_authority->negotiationLoop(m_running);
}

void GameServer::handleAuthorityClaim(uint16_t pid) {
    if (!m_authority) return;
    bool accepted = m_authority->onClientClaim(pid);
    if (!accepted) {
        // Send a gentle rejection via chat rather than a new packet type
        sendChat(pid, "[Server] AI authority not available right now.");
    }
}

void GameServer::grantAuthority(uint16_t pid) {
    // Tell the client to start streaming AI states
    json j = {
        {"ai_count",       m_cfg.ai_count},
        {"speed_limit",    m_cfg.ai_speed_limit},
        {"mode",           m_cfg.ai_mode},
        {"update_hz",      m_cfg.ai_update_rate_hz},
    };
    std::lock_guard lk(m_players_mutex);
    for (auto& [sid, p] : m_session_to_player)
        if (p == pid) {
            m_tcp->send(sid,
                Packet::buildStr(PacketType::AUTHORITY_GRANT, 0, 0, j.dump()));
            break;
        }
}

void GameServer::revokeAuthority(uint16_t pid) {
    std::lock_guard lk(m_players_mutex);
    for (auto& [sid, p] : m_session_to_player)
        if (p == pid) {
            m_tcp->send(sid,
                Packet::buildStr(PacketType::AUTHORITY_REVOKE, 0, 0, "{}"));
            break;
        }
}

void GameServer::writeBridgeConfig() {
    // Write /userdata/novaMP_bridge.json inside BeamNG's user directory.
    // For the server-side headless instance we write to the working directory
    // and pass -userpath to BeamNG so it picks up the file.
    json j = {
        {"host",          "127.0.0.1"},
        {"port",          m_cfg.port},
        {"bridge_token",  m_cfg.bridge_token},
        {"ai_count",      m_cfg.ai_count},
        {"ai_speed_limit",m_cfg.ai_speed_limit},
        {"ai_mode",       m_cfg.ai_mode},
        {"ai_hz",         m_cfg.ai_update_rate_hz},
        {"vehicle_pool",  m_cfg.ai_vehicle_pool},
    };
    try {
        std::ofstream f("novaMP_bridge.json");
        f << j.dump(2);
        spdlog::info("[Authority] Bridge config written to novaMP_bridge.json");
    } catch (const std::exception& e) {
        spdlog::warn("[Authority] Could not write bridge config: {}", e.what());
    }
}

} // namespace novaMP
