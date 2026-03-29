// client/src/ui/server_browser.cpp
#include "server_browser.hpp"
#include "../compat/beammp_client.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <asio.hpp>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <string>

namespace novaMP {
using json = nlohmann::json;

// ── Minimal HTTP GET (plain HTTP only — NovaMP master is HTTP) ────────────────
static std::string http_get(const std::string& url) {
    std::string host, path;
    int port = 80;

    auto after_scheme = url.find("://");
    std::string rest  = (after_scheme != std::string::npos)
                        ? url.substr(after_scheme + 3) : url;
    auto slash = rest.find('/');
    if (slash != std::string::npos) {
        host = rest.substr(0, slash);
        path = rest.substr(slash);
    } else { host = rest; path = "/"; }
    auto colon = host.rfind(':');
    if (colon != std::string::npos) {
        port = std::stoi(host.substr(colon + 1));
        host = host.substr(0, colon);
    }

    asio::io_context ioc;
    asio::ip::tcp::resolver resolver(ioc);
    auto endpoints = resolver.resolve(host, std::to_string(port));
    asio::ip::tcp::socket sock(ioc);
    asio::connect(sock, endpoints);

    std::string req =
        "GET " + path + " HTTP/1.0\r\n"
        "Host: " + host + "\r\n"
        "Connection: close\r\n\r\n";
    asio::write(sock, asio::buffer(req));

    std::string response;
    std::error_code ec;
    char buf[4096];
    while (true) {
        size_t n = sock.read_some(asio::buffer(buf), ec);
        if (n > 0) response.append(buf, n);
        if (ec) break;
    }

    auto body_start = response.find("\r\n\r\n");
    return body_start != std::string::npos
           ? response.substr(body_start + 4) : response;
}

// ── Constructor ───────────────────────────────────────────────────────────────
ServerBrowser::ServerBrowser(const std::string& master_url, bool fetch_beammp)
    : m_master_url(master_url), m_fetch_beammp(fetch_beammp) {}

// ── Refresh ───────────────────────────────────────────────────────────────────
bool ServerBrowser::refresh() {
    m_servers.clear();
    bool nova_ok  = fetchNovaMP();
    bool beam_ok  = m_fetch_beammp && fetchBeamMP();

    spdlog::info("Server browser: {} NovaMP + {} BeamMP servers.",
        std::count_if(m_servers.begin(), m_servers.end(),
            [](auto& s){ return s.network == ServerNetwork::NOVAMP; }),
        std::count_if(m_servers.begin(), m_servers.end(),
            [](auto& s){ return s.network == ServerNetwork::BEAMMP; }));

    return nova_ok || beam_ok;
}

bool ServerBrowser::fetchNovaMP() {
    try {
        auto body = http_get(m_master_url + "/servers");
        auto j    = json::parse(body);
        for (auto& s : j["servers"]) {
            ServerEntry e;
            e.id                = s.value("id",          0LL);
            e.name              = s.value("name",         "");
            e.description       = s.value("description",  "");
            e.host              = s.value("host",          "");
            e.port              = s.value("port",         4444);
            e.map               = s.value("map",           "");
            e.current_players   = s.value("current_players", 0);
            e.max_players       = s.value("max_players",   0);
            e.password_protected= s.value("password",      false);
            e.version           = s.value("version",       "");
            e.network           = ServerNetwork::NOVAMP;
            m_servers.push_back(e);
        }
        return true;
    } catch (const std::exception& ex) {
        spdlog::warn("NovaMP server list failed: {}", ex.what());
        return false;
    }
}

bool ServerBrowser::fetchBeamMP() {
    try {
        auto entries = beammp::BeamMPClient::fetchServerList();
        for (auto& b : entries) {
            ServerEntry e;
            e.name               = b.name;
            e.description        = b.description;
            e.host               = b.host;
            e.port               = b.port;
            e.map                = b.map;
            e.version            = b.version;
            e.current_players    = b.current_players;
            e.max_players        = b.max_players;
            e.password_protected = b.password_protected;
            e.modded             = b.modded;
            e.network            = ServerNetwork::BEAMMP;
            m_servers.push_back(e);
        }
        return !entries.empty();
    } catch (const std::exception& ex) {
        spdlog::warn("BeamMP server list failed: {}", ex.what());
        return false;
    }
}

// ── Console UI ────────────────────────────────────────────────────────────────
void ServerBrowser::runConsoleUI(SelectCallback on_select) {
    if (m_servers.empty()) {
        std::cout << "No servers found on any network.\n";
        return;
    }

    // Filter options
    std::cout << "\nFilter: [1] All  [2] NovaMP only  [3] BeamMP only  > ";
    int filter = 1;
    std::cin >> filter;
    std::cin.ignore();

    std::vector<const ServerEntry*> visible;
    for (auto& s : m_servers) {
        if (filter == 2 && s.network != ServerNetwork::NOVAMP)  continue;
        if (filter == 3 && s.network != ServerNetwork::BEAMMP)  continue;
        visible.push_back(&s);
    }

    if (visible.empty()) {
        std::cout << "No servers match the filter.\n";
        return;
    }

    // Sort: NovaMP first, then by player count descending
    std::stable_sort(visible.begin(), visible.end(), [](auto* a, auto* b) {
        if (a->network != b->network)
            return a->network == ServerNetwork::NOVAMP;
        return a->current_players > b->current_players;
    });

    // Header
    std::cout << "\n"
              << std::left
              << std::setw(4)  << "#"
              << std::setw(6)  << "Net"
              << std::setw(30) << "Name"
              << std::setw(20) << "Map"
              << std::setw(9)  << "Players"
              << std::setw(10) << "Flags"
              << "\n"
              << std::string(79, '-') << "\n";

    for (size_t i = 0; i < visible.size(); ++i) {
        auto* s = visible[i];
        std::string net     = (s->network == ServerNetwork::NOVAMP) ? "NMP" : "BMP";
        std::string players = std::to_string(s->current_players)
                            + "/" + std::to_string(s->max_players);
        std::string flags;
        if (s->password_protected) flags += "[P]";
        if (s->modded)             flags += "[M]";

        std::cout << std::setw(4)  << (i + 1)
                  << std::setw(6)  << net
                  << std::setw(30) << s->name.substr(0, 29)
                  << std::setw(20) << s->map.substr(0, 19)
                  << std::setw(9)  << players
                  << std::setw(10) << flags
                  << "\n";
    }

    std::cout << "\n[NMP]=NovaMP  [BMP]=BeamMP  [P]=Password  [M]=Mods\n"
              << "Select server (0 to cancel): ";
    int choice = 0;
    std::cin >> choice;
    std::cin.ignore();
    if (choice < 1 || choice > (int)visible.size()) return;
    on_select(*visible[choice - 1]);
}

} // namespace novaMP
