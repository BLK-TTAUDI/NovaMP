// servers/src/console/console.cpp
#include "console.hpp"
#include "../game_server.hpp"
#include <spdlog/spdlog.h>
#include <iostream>
#include <sstream>

namespace novaMP {

Console::Console(GameServer& server) : m_server(server) {}

void Console::run() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!dispatch(line)) break;
    }
}

bool Console::dispatch(const std::string& line) {
    if (line.empty()) return true;
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;

    if (cmd == "stop" || cmd == "quit" || cmd == "exit") {
        m_server.stop();
        return false;
    }
    if (cmd == "help")    { printHelp(); return true; }

    if (cmd == "players") {
        spdlog::info("Players: {}/{}", m_server.playerCount(), m_server.config().max_players);
        for (auto& [id, p] : m_server.players())
            spdlog::info("  [{}] {} ({})", id, p->username, p->ip);
        return true;
    }

    if (cmd == "kick") {
        uint16_t pid; std::string reason;
        ss >> pid; std::getline(ss, reason);
        if (!reason.empty() && reason[0] == ' ') reason = reason.substr(1);
        if (reason.empty()) reason = "Kicked by admin";
        m_server.kickPlayer(pid, reason);
        return true;
    }

    if (cmd == "ban") {
        uint16_t pid; std::string reason;
        ss >> pid; std::getline(ss, reason);
        if (!reason.empty() && reason[0] == ' ') reason = reason.substr(1);
        if (reason.empty()) reason = "Banned by admin";
        m_server.banPlayer(pid, reason);
        return true;
    }

    if (cmd == "say") {
        std::string msg; std::getline(ss, msg);
        if (!msg.empty() && msg[0] == ' ') msg = msg.substr(1);
        m_server.sendChat(-1, "[Admin] " + msg);
        return true;
    }

    if (cmd == "lua") {
        std::string code; std::getline(ss, code);
        spdlog::info("Lua: {}", m_server.lua().exec(code));
        return true;
    }

    if (cmd == "ai.spawn") {
        m_server.aiTraffic().spawnVehicle("etk800", 0, 0, 0);
        return true;
    }
    if (cmd == "ai.despawn_all") {
        m_server.aiTraffic().despawnAll();
        return true;
    }
    if (cmd == "ai.set_count") {
        int n = 0; ss >> n;
        m_server.aiTraffic().setCount(n);
        return true;
    }
    if (cmd == "ai.set_speed_limit") {
        float v = 0; ss >> v;
        m_server.aiTraffic().setSpeedLimit(v);
        return true;
    }
    if (cmd == "ai.status") {
        spdlog::info("AI active: {}", m_server.aiTraffic().activeCount());
        return true;
    }

    spdlog::warn("Unknown command '{}'. Type 'help'.", cmd);
    return true;
}

void Console::printHelp() {
    spdlog::info("Commands:");
    spdlog::info("  stop / quit            — shut down");
    spdlog::info("  players                — list players");
    spdlog::info("  kick <id> [reason]     — kick a player");
    spdlog::info("  ban  <id> [reason]     — ban a player");
    spdlog::info("  say  <msg>             — broadcast chat");
    spdlog::info("  lua  <code>            — run Lua");
    spdlog::info("  ai.spawn               — spawn one AI vehicle");
    spdlog::info("  ai.despawn_all         — remove all AI vehicles");
    spdlog::info("  ai.set_count <n>       — set AI count");
    spdlog::info("  ai.set_speed_limit <v> — set AI speed (m/s)");
    spdlog::info("  ai.status              — show active AI count");
}

} // namespace novaMP
