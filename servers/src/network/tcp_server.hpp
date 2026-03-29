// servers/src/network/tcp_server.hpp
#pragma once

#include <asio.hpp>
#include <functional>
#include <unordered_map>
#include <memory>
#include <vector>
#include <mutex>
#include <cstdint>
#include <string>

namespace novaMP {

class TCPSession;

class TCPServer {
public:
    using RecvCallback       = std::function<void(uint16_t session_id, const uint8_t* data, size_t len)>;
    using DisconnectCallback = std::function<void(uint16_t session_id)>;

    TCPServer(asio::io_context& ioc, uint16_t port,
              RecvCallback recv_cb, DisconnectCallback disc_cb);

    void send(uint16_t session_id, const std::vector<uint8_t>& data);
    void sendAll(const std::vector<uint8_t>& data, uint16_t exclude = 0xFFFF);
    void disconnect(uint16_t session_id);

    std::string getRemoteIP(uint16_t session_id) const;

private:
    asio::ip::tcp::acceptor m_acceptor;
    RecvCallback            m_recv_cb;
    DisconnectCallback      m_disc_cb;
    uint16_t                m_next_session_id = 1;

    std::unordered_map<uint16_t, std::shared_ptr<TCPSession>> m_sessions;
    mutable std::mutex m_sessions_mutex;

    void startAccept();
    void onSessionClose(uint16_t id);
};

} // namespace novaMP
