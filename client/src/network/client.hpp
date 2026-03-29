// client/src/network/client.hpp
#pragma once

#include <asio.hpp>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <array>
#include "packet.hpp"

namespace novaMP {

struct ServerInfo {
    std::string name, description, map, version;
    int  current_players = 0, max_players = 0;
    bool password_protected = false, ai_enabled = false;
};

class NetworkClient {
public:
    using PacketCallback     = std::function<void(PacketType, const uint8_t*, size_t)>;
    using ConnectCallback    = std::function<void(bool, const std::string&)>;
    using DisconnectCallback = std::function<void(const std::string&)>;

    NetworkClient();
    ~NetworkClient();

    void setPacketCallback    (PacketCallback cb)    { m_pkt_cb  = std::move(cb); }
    void setConnectCallback   (ConnectCallback cb)   { m_conn_cb = std::move(cb); }
    void setDisconnectCallback(DisconnectCallback cb){ m_disc_cb = std::move(cb); }

    void connect(const std::string& host, uint16_t port,
                 const std::string& username,
                 const std::string& jwt_token     = "",
                 const std::string& server_password = "");

    void disconnect();
    bool isConnected() const { return m_connected; }

    void sendTCP(const std::vector<uint8_t>& data);
    void sendTCP(PacketType t, const std::string& payload);
    void sendUDP(const std::vector<uint8_t>& data);
    void sendVehicleUpdate(const VehicleState& vs);

    uint16_t           playerID()    const { return m_player_id; }
    const ServerInfo&  serverInfo()  const { return m_server_info; }

private:
    asio::io_context   m_ioc;
    std::thread        m_ioc_thread;

    asio::ip::tcp::socket   m_tcp_socket;
    asio::ip::udp::socket   m_udp_socket;
    asio::ip::udp::endpoint m_udp_ep;

    std::array<uint8_t, 4>     m_tcp_len_buf;
    std::vector<uint8_t>       m_tcp_payload_buf;
    std::array<uint8_t, 65507> m_udp_recv_buf;

    std::atomic<bool> m_connected{false};
    uint16_t m_player_id = 0;
    uint32_t m_tcp_seq   = 0;
    uint32_t m_udp_seq   = 0;
    ServerInfo m_server_info;

    PacketCallback     m_pkt_cb;
    ConnectCallback    m_conn_cb;
    DisconnectCallback m_disc_cb;

    void tcpReadLen();
    void tcpReadPayload();
    void udpRecv();
    void onPacket(const uint8_t* data, size_t len);
};

} // namespace novaMP
