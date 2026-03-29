// servers/src/console/console.hpp
#pragma once
#include <string>

namespace novaMP {

class GameServer;

class Console {
public:
    explicit Console(GameServer& server);
    void run();                          // blocking — call from its own thread
    bool dispatch(const std::string& line); // returns false on "stop"

private:
    GameServer& m_server;
    void printHelp();
};

} // namespace novaMP
