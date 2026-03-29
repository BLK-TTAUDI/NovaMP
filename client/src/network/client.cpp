// client/src/network/client.cpp
#include "client.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace novaMP {
using json = nlohmann::json;

NetworkClient::NetworkClient()
    : m_tcp_socket(m_ioc), m_udp_socket(m_ioc) {}

NetworkClient::~NetworkClient() { disconnect(); }

void NetworkClient::connect(const std::string& host, uint16_t port,
                             const std::string& username,
                             const std::string& jwt_token,
                             const std::string& server_password)
{
    asio::ip::tcp::resolver resolver(m_ioc);
    auto endpoints = resolver.resolve(host, std::to_string(port));

    asio::async_connect(m_tcp_socket, endpoints,
        [this, username, jwt_token, server_password, host, port]
        (std::error_code ec, asio::ip::tcp::endpoint)
        {
            if (ec) { if (m_conn_cb) m_conn_cb(false, ec.message()); return; }

            m_tcp_socket.set_option(asio::ip::tcp::no_delay(true));

            m_udp_socket.open(asio::ip::udp::v4());
            m_udp_ep = asio::ip::udp::endpoint(
                asio::ip::make_address(
                    m_tcp_socket.remote_endpoint().address().to_string()), port);

            json auth = {
                {"username",        username},
                {"token",           jwt_token},
                {"server_password", server_password},
                {"version",         "1.0.0"}
            };
            sendTCP(PacketType::AUTH_REQUEST, auth.dump());
            tcpReadLen();
            udpRecv();
        });

    m_ioc_thread = std::thread([this] { m_ioc.run(); });
}

void NetworkClient::disconnect() {
    m_connected = false;
    std::error_code ec;
    m_tcp_socket.close(ec);
    m_udp_socket.close(ec);
    m_ioc.stop();
    if (m_ioc_thread.joinable()) m_ioc_thread.join();
}

void NetworkClient::sendTCP(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> framed(4 + data.size());
    uint32_t len = (uint32_t)data.size();
    memcpy(framed.data(), &len, 4);
    memcpy(framed.data() + 4, data.data(), data.size());
    asio::async_write(m_tcp_socket, asio::buffer(framed),
        [](std::error_code, size_t) {});
}

void NetworkClient::sendTCP(PacketType t, const std::string& payload) {
    sendTCP(Packet::buildStr(t, m_player_id, ++m_tcp_seq, payload));
}

void NetworkClient::sendUDP(const std::vector<uint8_t>& data) {
    m_udp_socket.async_send_to(asio::buffer(data), m_udp_ep,
        [](std::error_code, size_t) {});
}

void NetworkClient::sendVehicleUpdate(const VehicleState& vs) {
    sendUDP(Packet::buildVehicleUpdate(m_player_id, ++m_udp_seq, vs));
}

void NetworkClient::tcpReadLen() {
    asio::async_read(m_tcp_socket, asio::buffer(m_tcp_len_buf),
        [this](std::error_code ec, size_t) {
            if (ec) { if (m_disc_cb) m_disc_cb(ec.message()); return; }
            uint32_t len; memcpy(&len, m_tcp_len_buf.data(), 4);
            if (len == 0 || len > 4*1024*1024) { disconnect(); return; }
            m_tcp_payload_buf.resize(len);
            tcpReadPayload();
        });
}

void NetworkClient::tcpReadPayload() {
    asio::async_read(m_tcp_socket, asio::buffer(m_tcp_payload_buf),
        [this](std::error_code ec, size_t) {
            if (ec) { if (m_disc_cb) m_disc_cb(ec.message()); return; }
            onPacket(m_tcp_payload_buf.data(), m_tcp_payload_buf.size());
            tcpReadLen();
        });
}

void NetworkClient::udpRecv() {
    m_udp_socket.async_receive_from(
        asio::buffer(m_udp_recv_buf), m_udp_ep,
        [this](std::error_code ec, size_t n) {
            if (!ec && n >= Packet::HEADER_SIZE)
                onPacket(m_udp_recv_buf.data(), n);
            udpRecv();
        });
}

void NetworkClient::onPacket(const uint8_t* data, size_t len) {
    auto hdr  = Packet::parseHeader(data, len);
    auto type = static_cast<PacketType>(hdr.type);

    if (type == PacketType::AUTH_RESPONSE) {
        std::string body(reinterpret_cast<const char*>(Packet::payload(data)),
                         hdr.payload_len);
        try {
            auto j = json::parse(body);
            if (j.value("ok", false)) {
                m_player_id = j.value("player_id", (uint16_t)0);
                m_connected = true;
                spdlog::info("Authenticated! Player ID={}", m_player_id);
                if (m_conn_cb) m_conn_cb(true, "");
            } else {
                if (m_conn_cb) m_conn_cb(false, j.value("error", "Unknown error"));
            }
        } catch (...) {}
    }

    if (type == PacketType::SERVER_INFO) {
        std::string body(reinterpret_cast<const char*>(Packet::payload(data)),
                         hdr.payload_len);
        try {
            auto j = json::parse(body);
            m_server_info.name            = j.value("name", "");
            m_server_info.map             = j.value("map", "");
            m_server_info.max_players     = j.value("max_players", 0);
            m_server_info.current_players = j.value("current", 0);
            m_server_info.ai_enabled      = j.value("ai_enabled", false);
        } catch (...) {}
    }

    if (m_pkt_cb) m_pkt_cb(type, data, len);
}

} // namespace novaMP
