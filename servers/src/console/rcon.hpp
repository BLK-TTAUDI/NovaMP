// servers/src/console/rcon.hpp
#pragma once
#include <asio.hpp>
#include <string>

namespace novaMP {
class GameServer;

class RconServer {
public:
    RconServer(GameServer& srv, asio::io_context& ioc,
               uint16_t port, const std::string& password);
private:
    GameServer&             m_server;
    asio::ip::tcp::acceptor m_acceptor;
    std::string             m_password;
    void accept();
};
} // namespace novaMP
