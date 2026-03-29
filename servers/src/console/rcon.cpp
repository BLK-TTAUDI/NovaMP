// servers/src/console/rcon.cpp
#include "rcon.hpp"
#include "../game_server.hpp"
#include "console.hpp"
#include <spdlog/spdlog.h>
#include <memory>

namespace novaMP {

RconServer::RconServer(GameServer& srv, asio::io_context& ioc,
                       uint16_t port, const std::string& password)
    : m_server(srv)
    , m_acceptor(ioc, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
    , m_password(password)
{
    accept();
    spdlog::info("RCON listening on port {}", port);
}

void RconServer::accept() {
    auto sock = std::make_shared<asio::ip::tcp::socket>(m_acceptor.get_executor());
    m_acceptor.async_accept(*sock, [this, sock](std::error_code ec) {
        if (!ec) {
            spdlog::info("RCON: connection from {}",
                         sock->remote_endpoint().address().to_string());

            auto buf = std::make_shared<std::string>();
            asio::async_read_until(*sock, asio::dynamic_buffer(*buf), '\n',
                [this, sock, buf](std::error_code ec2, size_t n) {
                    if (ec2) return;
                    std::string pass = buf->substr(0, n);
                    buf->erase(0, n);
                    while (!pass.empty() && (pass.back()=='\n'||pass.back()=='\r'))
                        pass.pop_back();

                    if (pass != m_password) {
                        asio::write(*sock, asio::buffer(std::string("ERR: Bad password\n")));
                        return;
                    }
                    asio::write(*sock, asio::buffer(std::string("OK\n")));

                    // Read commands in a loop
                    std::function<void()> readCmd = [this, sock, buf, &readCmd]() {
                        asio::async_read_until(*sock, asio::dynamic_buffer(*buf), '\n',
                            [this, sock, buf, &readCmd](std::error_code ec3, size_t n2) {
                                if (ec3) return;
                                std::string cmd = buf->substr(0, n2);
                                buf->erase(0, n2);
                                while (!cmd.empty() && (cmd.back()=='\n'||cmd.back()=='\r'))
                                    cmd.pop_back();

                                Console console(m_server);
                                console.dispatch(cmd);
                                asio::write(*sock, asio::buffer(std::string("OK\n")));
                                readCmd();
                            });
                    };
                    readCmd();
                });
        }
        accept();
    });
}

} // namespace novaMP
