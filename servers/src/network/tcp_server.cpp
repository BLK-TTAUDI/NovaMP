// servers/src/network/tcp_server.cpp
#include "tcp_server.hpp"
#include "packet.hpp"
#include <spdlog/spdlog.h>

namespace novaMP {

// ── TCPSession ────────────────────────────────────────────────────────────────
class TCPSession : public std::enable_shared_from_this<TCPSession> {
public:
    TCPSession(asio::ip::tcp::socket sock, uint16_t id,
               TCPServer::RecvCallback recv_cb,
               std::function<void(uint16_t)> close_cb)
        : m_socket(std::move(sock)), m_id(id)
        , m_recv_cb(std::move(recv_cb)), m_close_cb(std::move(close_cb))
    {}

    void start() { readLen(); }

    void send(const std::vector<uint8_t>& data) {
        std::vector<uint8_t> framed(4 + data.size());
        uint32_t len = (uint32_t)data.size();
        memcpy(framed.data(), &len, 4);
        memcpy(framed.data() + 4, data.data(), data.size());
        auto self = shared_from_this();
        asio::async_write(m_socket, asio::buffer(framed),
            [self](std::error_code ec, size_t) { if (ec) self->close(); });
    }

    void close() {
        std::error_code ec;
        m_socket.close(ec);
        m_close_cb(m_id);
    }

    std::string remoteIP() const {
        try { return m_socket.remote_endpoint().address().to_string(); }
        catch (...) { return ""; }
    }

    uint16_t id() const { return m_id; }

private:
    asio::ip::tcp::socket         m_socket;
    uint16_t                      m_id;
    TCPServer::RecvCallback       m_recv_cb;
    std::function<void(uint16_t)> m_close_cb;

    std::array<uint8_t, 4> m_len_buf;
    std::vector<uint8_t>   m_payload_buf;

    void readLen() {
        auto self = shared_from_this();
        asio::async_read(m_socket, asio::buffer(m_len_buf),
            [self](std::error_code ec, size_t) {
                if (ec) { self->close(); return; }
                uint32_t len;
                memcpy(&len, self->m_len_buf.data(), 4);
                if (len == 0 || len > 4 * 1024 * 1024) { self->close(); return; }
                self->m_payload_buf.resize(len);
                self->readPayload();
            });
    }

    void readPayload() {
        auto self = shared_from_this();
        asio::async_read(m_socket, asio::buffer(m_payload_buf),
            [self](std::error_code ec, size_t) {
                if (ec) { self->close(); return; }
                self->m_recv_cb(self->m_id,
                    self->m_payload_buf.data(),
                    self->m_payload_buf.size());
                self->readLen();
            });
    }
};

// ── TCPServer ─────────────────────────────────────────────────────────────────
TCPServer::TCPServer(asio::io_context& ioc, uint16_t port,
                     RecvCallback recv_cb, DisconnectCallback disc_cb)
    : m_acceptor(ioc, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
    , m_recv_cb(std::move(recv_cb))
    , m_disc_cb(std::move(disc_cb))
{
    m_acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    startAccept();
    spdlog::info("TCP server listening on port {}", port);
}

void TCPServer::startAccept() {
    m_acceptor.async_accept([this](std::error_code ec, asio::ip::tcp::socket sock) {
        if (!ec) {
            sock.set_option(asio::ip::tcp::no_delay(true));
            uint16_t sid = m_next_session_id++;
            auto session = std::make_shared<TCPSession>(
                std::move(sock), sid, m_recv_cb,
                [this](uint16_t id) { onSessionClose(id); });
            {
                std::lock_guard lk(m_sessions_mutex);
                m_sessions[sid] = session;
            }
            session->start();
            spdlog::debug("TCP: New connection session_id={}", sid);
        }
        startAccept();
    });
}

void TCPServer::send(uint16_t session_id, const std::vector<uint8_t>& data) {
    std::lock_guard lk(m_sessions_mutex);
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end()) it->second->send(data);
}

void TCPServer::sendAll(const std::vector<uint8_t>& data, uint16_t exclude) {
    std::lock_guard lk(m_sessions_mutex);
    for (auto& [id, session] : m_sessions)
        if (id != exclude) session->send(data);
}

void TCPServer::disconnect(uint16_t session_id) {
    std::lock_guard lk(m_sessions_mutex);
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end()) it->second->close();
}

std::string TCPServer::getRemoteIP(uint16_t session_id) const {
    std::lock_guard lk(m_sessions_mutex);
    auto it = m_sessions.find(session_id);
    return it != m_sessions.end() ? it->second->remoteIP() : "";
}

void TCPServer::onSessionClose(uint16_t id) {
    { std::lock_guard lk(m_sessions_mutex); m_sessions.erase(id); }
    m_disc_cb(id);
}

} // namespace novaMP
