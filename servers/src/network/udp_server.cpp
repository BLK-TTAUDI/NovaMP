// servers/src/network/udp_server.cpp
#include "udp_server.hpp"
#include "packet.hpp"
#include <spdlog/spdlog.h>

namespace novaMP {

UDPServer::UDPServer(asio::io_context& ioc, uint16_t port, RecvCallback cb)
    : m_socket(ioc, asio::ip::udp::endpoint(asio::ip::udp::v4(), port))
    , m_recv_cb(std::move(cb))
{
    m_socket.set_option(asio::socket_base::receive_buffer_size(1 << 20));
    m_socket.set_option(asio::socket_base::send_buffer_size(1 << 20));
    startRecv();
    spdlog::info("UDP server listening on port {}", port);
}

void UDPServer::startRecv() {
    m_socket.async_receive_from(
        asio::buffer(m_recv_buf), m_remote_ep,
        [this](std::error_code ec, size_t n) {
            if (!ec && n >= Packet::HEADER_SIZE) {
                UDPEndpoint ep{
                    m_remote_ep.address().to_string(),
                    m_remote_ep.port()
                };
                m_recv_cb(ep, m_recv_buf.data(), n);
            }
            startRecv();
        });
}

void UDPServer::send(const UDPEndpoint& to, const std::vector<uint8_t>& data) {
    asio::ip::udp::endpoint ep(asio::ip::make_address(to.ip), to.port);
    m_socket.async_send_to(asio::buffer(data), ep,
        [](std::error_code, size_t) {});
}

void UDPServer::sendAll(const std::vector<uint8_t>& data, uint16_t exclude) {
    std::lock_guard lk(m_map_mutex);
    for (auto& [pid, ep] : m_player_to_ep) {
        if (pid == exclude) continue;
        asio::ip::udp::endpoint aep(asio::ip::make_address(ep.ip), ep.port);
        m_socket.async_send_to(asio::buffer(data), aep,
            [](std::error_code, size_t) {});
    }
}

void UDPServer::mapPlayer(uint16_t player_id, const UDPEndpoint& ep) {
    std::lock_guard lk(m_map_mutex);
    m_player_to_ep[player_id] = ep;
    m_ep_to_player[ep]        = player_id;
}

void UDPServer::unmapPlayer(uint16_t player_id) {
    std::lock_guard lk(m_map_mutex);
    auto it = m_player_to_ep.find(player_id);
    if (it != m_player_to_ep.end()) {
        m_ep_to_player.erase(it->second);
        m_player_to_ep.erase(it);
    }
}

uint16_t UDPServer::playerFromEndpoint(const UDPEndpoint& ep) const {
    std::lock_guard lk(m_map_mutex);
    auto it = m_ep_to_player.find(ep);
    return it != m_ep_to_player.end() ? it->second : 0xFFFF;
}

} // namespace novaMP
