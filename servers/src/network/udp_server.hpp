// servers/src/network/udp_server.hpp
#pragma once

#include <asio.hpp>
#include <functional>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <mutex>
#include <string>

namespace novaMP {

struct UDPEndpoint {
    std::string ip;
    uint16_t    port;
    bool operator==(const UDPEndpoint& o) const { return ip == o.ip && port == o.port; }
};

struct UDPEndpointHash {
    size_t operator()(const UDPEndpoint& e) const {
        return std::hash<std::string>{}(e.ip + ":" + std::to_string(e.port));
    }
};

class UDPServer {
public:
    using RecvCallback = std::function<void(
        const UDPEndpoint& from, const uint8_t* data, size_t len)>;

    UDPServer(asio::io_context& ioc, uint16_t port, RecvCallback cb);

    void send(const UDPEndpoint& to, const std::vector<uint8_t>& data);
    void sendAll(const std::vector<uint8_t>& data, uint16_t exclude_player_id = 0xFFFF);

    void     mapPlayer(uint16_t player_id, const UDPEndpoint& ep);
    void     unmapPlayer(uint16_t player_id);
    uint16_t playerFromEndpoint(const UDPEndpoint& ep) const;

private:
    asio::ip::udp::socket      m_socket;
    RecvCallback               m_recv_cb;
    std::array<uint8_t, 65507> m_recv_buf;
    asio::ip::udp::endpoint    m_remote_ep;

    std::unordered_map<uint16_t, UDPEndpoint>                    m_player_to_ep;
    std::unordered_map<UDPEndpoint, uint16_t, UDPEndpointHash>   m_ep_to_player;
    mutable std::mutex m_map_mutex;

    void startRecv();
};

} // namespace novaMP
