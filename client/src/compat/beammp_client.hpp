// client/src/compat/beammp_client.hpp
//
// BeamMPClient — connects to a BeamMP game server using their wire protocol.
//
// Provides the same connect/disconnect/send surface as NovaMP's NetworkClient
// so that client/src/main.cpp can switch transparently between the two.
//
// The client handles:
//   - TCP connection + BeamMP framed read loop
//   - Split-packet reassembly (CODE_SPLIT / CODE_SPLIT_FINISH)
//   - Handshake: wait for CODE_MAP, send player key, wait for player ID
//   - Forwarding vehicle transforms, chat, and spawn data to/from the Lua mod
//     via a local UDP loopback socket (same mechanism as NovaMP's main.lua)
#pragma once

#include "beammp_protocol.hpp"
#include "beammp_auth.hpp"
#include <asio.hpp>
#include <functional>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace novaMP::beammp {

// Mirrors NovaMP ServerEntry enough for the browser to work with both types
struct BeamMPServerEntry {
    std::string name;
    std::string description;
    std::string host;
    std::string map;
    std::string version;
    int         port            = 4444;
    int         current_players = 0;
    int         max_players     = 0;
    bool        password_protected = false;
    bool        modded          = false;
};

class BeamMPClient {
public:
    using ConnectCb    = std::function<void(bool ok, const std::string& err)>;
    using DisconnectCb = std::function<void(const std::string& reason)>;
    using ChatCb       = std::function<void(const std::string& message)>;
    using VehicleCb    = std::function<void(const BeamMPTransform&)>;
    using SpawnCb      = std::function<void(uint16_t pid, uint8_t vid,
                                            const std::string& model_data)>;
    using DeleteCb     = std::function<void(uint16_t pid, uint8_t vid)>;

    BeamMPClient();
    ~BeamMPClient();

    // Set callbacks before calling connect()
    void setConnectCallback   (ConnectCb    cb) { m_connect_cb    = std::move(cb); }
    void setDisconnectCallback(DisconnectCb cb) { m_disconnect_cb = std::move(cb); }
    void setChatCallback      (ChatCb       cb) { m_chat_cb       = std::move(cb); }
    void setVehicleCallback   (VehicleCb    cb) { m_vehicle_cb    = std::move(cb); }
    void setSpawnCallback     (SpawnCb      cb) { m_spawn_cb      = std::move(cb); }
    void setDeleteCallback    (DeleteCb     cb) { m_delete_cb     = std::move(cb); }

    // Connect to a BeamMP game server.
    // auth must already be authenticated (auth.isLoggedIn() == true).
    void connect   (const std::string& host, uint16_t port, const BeamMPAuth& auth);
    void disconnect();

    // Send our local vehicle transform to the server
    void sendTransform(uint16_t pid, uint8_t vid,
                       const float pos[3], const float rot[4],
                       const float vel[3], const float ang_vel[3]);

    void sendChat(const std::string& msg);

    bool isConnected() const { return m_connected; }
    uint16_t playerID() const { return m_player_id; }
    const std::string& mapName() const { return m_map_name; }

    // Fetch BeamMP public server list (blocking HTTP GET)
    static std::vector<BeamMPServerEntry> fetchServerList();

private:
    asio::io_context          m_ioc;
    asio::ip::tcp::socket     m_sock;
    std::vector<std::thread>  m_threads;
    std::atomic<bool>         m_connected{false};

    ConnectCb    m_connect_cb;
    DisconnectCb m_disconnect_cb;
    ChatCb       m_chat_cb;
    VehicleCb    m_vehicle_cb;
    SpawnCb      m_spawn_cb;
    DeleteCb     m_delete_cb;

    uint16_t    m_player_id = 0;
    std::string m_map_name;
    std::string m_private_key;

    // TCP receive buffer + split-packet reassembly
    std::vector<uint8_t> m_recv_buf;
    std::string          m_split_buf;
    bool                 m_in_split = false;

    void readLoop();
    void handlePacket(Code code, const std::string& data);
    void send(Code code, const std::string& data);

    // Returns body of HTTP response (strips headers)
    static std::string httpGet(const std::string& url);
};

} // namespace novaMP::beammp
