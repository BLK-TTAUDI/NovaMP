// servers/src/game_server.hpp
#pragma once

#include <memory>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <vector>

#include <asio.hpp>

#include "config/config.hpp"
#include "player.hpp"
#include "network/udp_server.hpp"
#include "network/tcp_server.hpp"
#include "sync/vehicle_sync.hpp"
#include "sync/ai_traffic.hpp"
#include "sync/ai_authority.hpp"
#include "headless_launcher.hpp"
#include "lua/lua_engine.hpp"
#include "console/console.hpp"
#include "console/rcon.hpp"

namespace novaMP {

class GameServer {
public:
    explicit GameServer(const ServerConfig& cfg);
    ~GameServer();

    void run();
    void stop();

    // Player access
    Player*  getPlayer(uint16_t id);
    int      playerCount() const;
    const std::unordered_map<uint16_t, std::unique_ptr<Player>>& players() const;

    void kickPlayer(uint16_t id, const std::string& reason);
    void banPlayer (uint16_t id, const std::string& reason);
    void sendChat  (int player_id, const std::string& msg); // -1 = all

    // Sub-system accessors
    LuaEngine&          lua()        { return *m_lua; }
    AITraffic&          aiTraffic()  { return *m_ai; }
    VehicleSync&        vehicleSync(){ return *m_vsync; }
    const ServerConfig& config() const { return m_cfg; }

private:
    ServerConfig     m_cfg;
    asio::io_context m_ioc;
    std::vector<std::thread> m_threads;
    std::atomic<bool>        m_running{false};

    std::unique_ptr<UDPServer>      m_udp;
    std::unique_ptr<TCPServer>      m_tcp;
    std::unique_ptr<VehicleSync>    m_vsync;
    std::unique_ptr<AITraffic>      m_ai;
    std::unique_ptr<AIAuthority>    m_authority;
    std::unique_ptr<HeadlessLauncher> m_headless;
    std::unique_ptr<LuaEngine>      m_lua;
    std::unique_ptr<Console>        m_console;
    std::unique_ptr<RconServer>     m_rcon;

    std::unordered_map<uint16_t, std::unique_ptr<Player>> m_players;
    std::unordered_map<uint16_t, uint16_t> m_session_to_player;
    mutable std::mutex m_players_mutex;
    uint16_t m_next_player_id = 1;

    // Packet handlers
    void onUDPPacket(const UDPEndpoint& from, const uint8_t* data, size_t len);
    void onTCPPacket(uint16_t session_id, const uint8_t* data, size_t len);
    void onDisconnect(uint16_t session_id);

    void handleAuth           (uint16_t session_id, const uint8_t* payload, uint16_t len);
    void handleVehicleSpawn   (uint16_t pid, const uint8_t* payload, uint16_t len);
    void handleVehicleDelete  (uint16_t pid, const uint8_t* payload, uint16_t len);
    void handleVehicleUpdate  (uint16_t pid, const uint8_t* payload, uint16_t len);
    void handleChat           (uint16_t pid, const uint8_t* payload, uint16_t len);
    void handleReady          (uint16_t pid);
    void handleAuthorityClaim (uint16_t pid);

    // Background loops
    void vehicleSyncLoop();
    void aiTrafficLoop();
    void heartbeatLoop();
    void authorityNegotiationLoop();

    // Helpers
    void writeBridgeConfig();
    void grantAuthority (uint16_t pid);
    void revokeAuthority(uint16_t pid);

    // Helpers
    void broadcastPlayerJoin (const Player& p);
    void broadcastPlayerLeave(uint16_t pid, const std::string& reason);
    void sendServerInfo      (uint16_t session_id);
    void sendExistingPlayers (uint16_t session_id);
    void masterHeartbeat();
    uint16_t allocPlayerID();
};

} // namespace novaMP
