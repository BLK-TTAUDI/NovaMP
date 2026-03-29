// client/src/main.cpp
// NovaMP Client Launcher — entry point
//
// Supports connecting to both NovaMP servers (native protocol) and
// BeamMP servers (BeamMP wire protocol with token caching fallback).

#include <iostream>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "launcher.hpp"
#include "network/client.hpp"
#include "ui/server_browser.hpp"
#include "compat/beammp_auth.hpp"
#include "compat/beammp_client.hpp"

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string promptLine(const std::string& label) {
    std::cout << label;
    std::string s;
    std::getline(std::cin, s);
    return s;
}

// ── NovaMP connection flow ────────────────────────────────────────────────────
static void runNovaMP(const novaMP::ServerEntry& server,
                      novaMP::Launcher& launcher,
                      const std::string& username,
                      const std::string& password)
{
    spdlog::info("Connecting to NovaMP server '{}' @ {}:{}",
                 server.name, server.host, server.port);

    launcher.launchGame(server.host, (uint16_t)server.port,
        [](const std::string& s){ spdlog::info("{}", s); });

    novaMP::NetworkClient client;

    client.setConnectCallback([&](bool ok, const std::string& err) {
        if (ok) spdlog::info("Connected! Player ID={}", client.playerID());
        else    spdlog::error("Connection failed: {}", err);
    });
    client.setDisconnectCallback([](const std::string& r) {
        spdlog::warn("Disconnected: {}", r);
    });
    client.setPacketCallback([&](novaMP::PacketType type,
                                  const uint8_t* data, size_t len)
    {
        using PT = novaMP::PacketType;
        auto hdr = novaMP::Packet::parseHeader(data, len);
        auto* pl = novaMP::Packet::payload(data);
        if (type == PT::CHAT_MESSAGE) {
            std::cout << "[Chat] "
                      << std::string(reinterpret_cast<const char*>(pl),
                                     hdr.payload_len) << "\n";
        } else if (type == PT::KICK) {
            spdlog::warn("Kicked: {}",
                std::string(reinterpret_cast<const char*>(pl), hdr.payload_len));
        }
    });

    client.connect(server.host, (uint16_t)server.port, username, "", "");

    std::cout << "\nPress Enter to disconnect...\n";
    std::cin.get();
    client.disconnect();
}

// ── BeamMP connection flow ────────────────────────────────────────────────────
static void runBeamMP(const novaMP::ServerEntry& server,
                      novaMP::Launcher& launcher,
                      const std::string& username,
                      const std::string& password)
{
    spdlog::info("Connecting to BeamMP server '{}' @ {}:{}",
                 server.name, server.host, server.port);

    // ── Authenticate ──────────────────────────────────────────────────────────
    novaMP::beammp::BeamMPAuth auth([](const std::string& s){
        spdlog::info("[BeamMP] {}", s);
    });

    bool authed = false;
    if (!password.empty()) {
        authed = auth.authenticate(username, password);
    } else {
        // Try cached token first (no password prompt if cache is valid)
        authed = auth.loadCached(username);
        if (!authed) {
            std::string pw = promptLine("BeamMP password: ");
            authed = auth.authenticate(username, pw);
        }
    }

    if (!authed) {
        spdlog::error("BeamMP authentication failed. Cannot connect.");
        std::cout << "Tip: if BeamMP's auth server is down, make sure you have "
                     "logged in at least once before so a cached token exists.\n";
        return;
    }

    // ── Launch BeamNG ─────────────────────────────────────────────────────────
    // We tell BeamNG to connect to the BeamMP server via the same
    // writeConnectConfig mechanism — the Lua mod reads the same JSON.
    launcher.launchGame(server.host, (uint16_t)server.port,
        [](const std::string& s){ spdlog::info("{}", s); });

    // ── Connect via BeamMP protocol ───────────────────────────────────────────
    novaMP::beammp::BeamMPClient client;

    client.setConnectCallback([&](bool ok, const std::string& err) {
        if (ok) {
            spdlog::info("[BeamMP] Connected! Player ID={}, Map={}",
                         client.playerID(), client.mapName());
        } else {
            spdlog::error("[BeamMP] Connection failed: {}", err);
        }
    });
    client.setDisconnectCallback([](const std::string& r) {
        spdlog::warn("[BeamMP] Disconnected: {}", r);
    });
    client.setChatCallback([](const std::string& msg) {
        std::cout << "[BeamMP Chat] " << msg << "\n";
    });
    client.setVehicleCallback([](const novaMP::beammp::BeamMPTransform& t) {
        // In a full implementation the Lua mod handles rendering;
        // here we just log at trace level to avoid console spam.
        spdlog::trace("[BeamMP] VehicleUpdate pid={} vid={}", t.player_id, t.vehicle_id);
    });
    client.setSpawnCallback([](uint16_t pid, uint8_t vid, const std::string& model) {
        spdlog::info("[BeamMP] Spawn pid={} vid={} model_len={}", pid, vid, model.size());
    });
    client.setDeleteCallback([](uint16_t pid, uint8_t vid) {
        spdlog::info("[BeamMP] Delete pid={} vid={}", pid, vid);
    });

    client.connect(server.host, (uint16_t)server.port, auth);

    std::cout << "\nConnected to BeamMP server '" << server.name << "'.\n"
              << "Press Enter to disconnect...\n";
    std::cin.get();
    client.disconnect();
}

// ── main ──────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    spdlog::set_default_logger(spdlog::stdout_color_mt("novaMP-client"));
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");

    std::cout
        << "╔══════════════════════════════════════╗\n"
        << "║   NovaMP Client  v1.0.0              ║\n"
        << "║   BeamMP compatibility enabled       ║\n"
        << "╚══════════════════════════════════════╝\n\n";

    std::string master_url = "http://master.novaMP.gg";

    std::string username = promptLine("Username: ");
    std::string password = promptLine("Password (NovaMP — leave blank for BeamMP-only): ");

    novaMP::LaunchConfig lcfg{
        "",                            // auto-detect BeamNG
        "Resources/Client/novaMP.zip", // bundled mod zip
        master_url,
        username,
        password
    };
    novaMP::Launcher launcher(lcfg);
    launcher.installMod([](const std::string& s){ spdlog::info("{}", s); });

    // ── Server selection ──────────────────────────────────────────────────────
    novaMP::ServerEntry selected;
    bool picked = false;

    if (argc >= 3) {
        // Direct connect: novaMP-client <ip> <port> [beammp]
        selected.host    = argv[1];
        selected.port    = std::stoi(argv[2]);
        selected.name    = argv[1];
        selected.network = (argc >= 4 && std::string(argv[3]) == "beammp")
                           ? novaMP::ServerNetwork::BEAMMP
                           : novaMP::ServerNetwork::NOVAMP;
        picked = true;
    } else {
        std::cout << "Fetching server lists (NovaMP + BeamMP)...\n";
        novaMP::ServerBrowser browser(master_url, /*fetch_beammp=*/true);
        browser.refresh();
        browser.runConsoleUI([&](const novaMP::ServerEntry& e) {
            selected = e;
            picked   = true;
        });
    }

    if (!picked) {
        std::cout << "No server selected. Exiting.\n";
        return 0;
    }

    // ── Route to the correct protocol ─────────────────────────────────────────
    if (selected.network == novaMP::ServerNetwork::BEAMMP) {
        runBeamMP(selected, launcher, username, password);
    } else {
        runNovaMP(selected, launcher, username, password);
    }

    return 0;
}
