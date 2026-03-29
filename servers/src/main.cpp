// servers/src/main.cpp
// NovaMP Dedicated Game Server entry point
#include <iostream>
#include <csignal>
#include "config/config.hpp"
#include "game_server.hpp"

static novaMP::GameServer* g_server = nullptr;

static void signal_handler(int) {
    if (g_server) g_server->stop();
}

int main(int argc, char* argv[]) {
    std::string cfg_path = argc > 1 ? argv[1] : "ServerConfig.toml";

    novaMP::ServerConfig cfg;
    try {
        cfg = novaMP::ServerConfig::load(cfg_path);
    } catch (const std::exception& e) {
        std::cerr << "Config error: " << e.what() << "\n";
        return 1;
    }

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        novaMP::GameServer server(cfg);
        g_server = &server;
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
