// client/src/compat/beammp_client.cpp
#include "beammp_client.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <stdexcept>
#include <cstring>

namespace novaMP::beammp {
using json = nlohmann::json;

// ── Constructor / Destructor ──────────────────────────────────────────────────
BeamMPClient::BeamMPClient()
    : m_sock(m_ioc) {}

BeamMPClient::~BeamMPClient() { disconnect(); }

// ── Connect ───────────────────────────────────────────────────────────────────
void BeamMPClient::connect(const std::string& host, uint16_t port,
                            const BeamMPAuth& auth)
{
    if (m_connected) disconnect();

    m_private_key = auth.privateKey();
    m_recv_buf.clear();
    m_split_buf.clear();
    m_in_split  = false;
    m_player_id = 0;
    m_map_name.clear();

    try {
        asio::ip::tcp::resolver resolver(m_ioc);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        asio::connect(m_sock, endpoints);
        m_connected = true;
    } catch (const std::exception& e) {
        if (m_connect_cb) m_connect_cb(false, e.what());
        return;
    }

    // Start the read loop on a background thread
    m_threads.emplace_back([this] { readLoop(); });
    // ASIO runner
    m_threads.emplace_back([this] { m_ioc.run(); });
}

void BeamMPClient::disconnect() {
    if (!m_connected.exchange(false)) return;
    std::error_code ec;
    m_sock.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    m_sock.close(ec);
    m_ioc.stop();
    for (auto& t : m_threads)
        if (t.joinable()) t.join();
    m_threads.clear();
    if (m_disconnect_cb) m_disconnect_cb("disconnected");
}

// ── Read loop ─────────────────────────────────────────────────────────────────
void BeamMPClient::readLoop() {
    // BeamMP frame: [code:1][size:3 LE][data:size]
    while (m_connected) {
        // Read header (4 bytes)
        uint8_t hdr[4];
        std::error_code ec;
        asio::read(m_sock, asio::buffer(hdr, 4), ec);
        if (ec) break;

        Code     code = static_cast<Code>(hdr[0]);
        uint32_t size = (uint32_t)hdr[1]
                      | ((uint32_t)hdr[2] << 8)
                      | ((uint32_t)hdr[3] << 16);

        // Guard against absurdly large frames (> 50 MB)
        if (size > 50 * 1024 * 1024) {
            spdlog::warn("[BeamMPClient] Oversized frame {} bytes — disconnecting.", size);
            break;
        }

        // Read payload
        std::string data(size, '\0');
        if (size > 0) {
            asio::read(m_sock, asio::buffer(data.data(), size), ec);
            if (ec) break;
        }

        // ── Split reassembly ─────────────────────────────────────────────────
        if (code == CODE_SPLIT) {
            m_in_split = true;
            m_split_buf += data;
            continue;
        }
        if (code == CODE_SPLIT_FINISH) {
            m_split_buf += data;
            // Re-parse: first byte of reassembled buf is the real code
            if (!m_split_buf.empty()) {
                Code real_code = static_cast<Code>((uint8_t)m_split_buf[0]);
                handlePacket(real_code, m_split_buf.substr(1));
            }
            m_split_buf.clear();
            m_in_split = false;
            continue;
        }

        handlePacket(code, data);
    }

    m_connected = false;
    if (m_disconnect_cb) m_disconnect_cb("connection closed");
}

// ── Packet handler ────────────────────────────────────────────────────────────
void BeamMPClient::handlePacket(Code code, const std::string& data) {
    switch (code) {

    // Server sends map name — we reply with player key
    case CODE_MAP: {
        m_map_name = data;
        spdlog::info("[BeamMPClient] Map: {}", m_map_name);
        // Send authentication packet: 'P' + private_key
        send(CODE_AUTH_PLAYER, m_private_key);
        break;
    }

    // Authentication response — format: "P<player_id>" or error string
    case CODE_AUTH_PLAYER: {
        if (!data.empty() && std::isdigit((unsigned char)data[0])) {
            try {
                m_player_id = (uint16_t)std::stoi(data);
                spdlog::info("[BeamMPClient] Authenticated! Player ID={}", m_player_id);
                // Signal ready
                send(CODE_READY, "");
                if (m_connect_cb) m_connect_cb(true, "");
            } catch (...) {}
        } else {
            // Auth rejected
            spdlog::warn("[BeamMPClient] Auth rejected: {}", data);
            if (m_connect_cb) m_connect_cb(false, data);
        }
        break;
    }

    // Kick
    case CODE_AUTH_KICK: {
        spdlog::warn("[BeamMPClient] Kicked: {}", data);
        m_connected = false;
        if (m_disconnect_cb) m_disconnect_cb("Kicked: " + data);
        break;
    }

    // Chat message
    case CODE_CHAT: {
        if (m_chat_cb) m_chat_cb(data);
        break;
    }

    // Vehicle transform update
    case CODE_SYNC: {
        if (m_vehicle_cb) {
            BeamMPTransform t;
            if (parseTransform(data, t)) m_vehicle_cb(t);
        }
        break;
    }

    // Vehicle spawn — format: "<pid>-<vid>:<jbeam_data_json>"
    case CODE_VEHICLE_SPAWN: {
        if (!m_spawn_cb) break;
        auto sep = data.find(':');
        if (sep == std::string::npos) break;
        std::string id_part   = data.substr(0, sep);
        std::string model_data= data.substr(sep + 1);
        auto dash = id_part.find('-');
        if (dash == std::string::npos) break;
        try {
            uint16_t pid = (uint16_t)std::stoi(id_part.substr(0, dash));
            uint8_t  vid = (uint8_t) std::stoi(id_part.substr(dash + 1));
            m_spawn_cb(pid, vid, model_data);
        } catch (...) {}
        break;
    }

    // Vehicle delete — format: "<pid>-<vid>"
    case CODE_VEHICLE_DEL: {
        if (!m_delete_cb) break;
        auto dash = data.find('-');
        if (dash == std::string::npos) break;
        try {
            uint16_t pid = (uint16_t)std::stoi(data.substr(0, dash));
            uint8_t  vid = (uint8_t) std::stoi(data.substr(dash + 1));
            m_delete_cb(pid, vid);
        } catch (...) {}
        break;
    }

    // Server info JSON
    case CODE_SERVER_INFO: {
        json j;
        try { j = json::parse(data); }
        catch (...) { break; }
        spdlog::info("[BeamMPClient] Server: {} | Map: {} | Players: {}/{}",
            j.value("name","?"), j.value("map","?"),
            j.value("players",0), j.value("maxPlayers",0));
        break;
    }

    // Wait — server is loading, just acknowledge
    case CODE_WAIT:
        send(CODE_OK, "");
        break;

    case CODE_PING:
        send(CODE_PONG, "");
        break;

    default:
        spdlog::debug("[BeamMPClient] Unhandled code 0x{:02X} ({} bytes)",
                      (uint8_t)code, data.size());
        break;
    }
}

// ── Send helpers ──────────────────────────────────────────────────────────────
void BeamMPClient::send(Code code, const std::string& data) {
    if (!m_connected) return;
    auto pkt = buildPacket(code, data);
    std::error_code ec;
    asio::write(m_sock, asio::buffer(pkt), ec);
    if (ec) {
        spdlog::warn("[BeamMPClient] Send error: {}", ec.message());
        m_connected = false;
    }
}

void BeamMPClient::sendTransform(uint16_t pid, uint8_t vid,
                                  const float pos[3], const float rot[4],
                                  const float vel[3], const float ang_vel[3])
{
    send(CODE_SYNC, encodeTransform(pid, vid, pos, rot, vel, ang_vel));
}

void BeamMPClient::sendChat(const std::string& msg) {
    send(CODE_CHAT, msg);
}

// ── Server list ───────────────────────────────────────────────────────────────
std::vector<BeamMPServerEntry> BeamMPClient::fetchServerList() {
    // BeamMP public server list — documented at https://www.beammp.com/
    std::vector<BeamMPServerEntry> out;
    try {
        auto body = httpGet("https://backend.beammp.com/servers-info");
        auto j    = json::parse(body);
        if (!j.is_array()) return out;

        for (auto& s : j) {
            BeamMPServerEntry e;
            e.name               = s.value("sname", "");
            e.description        = s.value("sdesc", "");
            e.host               = s.value("ip",    "");
            e.port               = s.value("port",  4444);
            e.map                = s.value("map",   "");
            e.version            = s.value("version","");
            e.current_players    = s.value("players", 0);
            e.max_players        = s.value("maxplayers", 0);
            e.password_protected = s.value("pass",  false);
            e.modded             = s.value("mods",  0) > 0;
            if (!e.host.empty()) out.push_back(e);
        }
    } catch (const std::exception& ex) {
        spdlog::warn("[BeamMPClient] Server list fetch failed: {}", ex.what());
    }
    return out;
}

std::string BeamMPClient::httpGet(const std::string& url) {
    std::string host, path;
    std::string scheme = "http";
    auto after = url.find("://");
    if (after != std::string::npos) {
        scheme = url.substr(0, after);
        after += 3;
    } else { after = 0; }
    std::string rest = url.substr(after);
    auto slash = rest.find('/');
    if (slash != std::string::npos) {
        host = rest.substr(0, slash);
        path = rest.substr(slash);
    } else { host = rest; path = "/"; }

    std::string port_str = (scheme == "https") ? "443" : "80";
    auto colon = host.rfind(':');
    if (colon != std::string::npos) {
        port_str = host.substr(colon + 1);
        host     = host.substr(0, colon);
    }

    asio::io_context ioc;
    std::string response;

    if (scheme == "https") {
        asio::ssl::context ssl_ctx(asio::ssl::context::tls_client);
        ssl_ctx.set_default_verify_paths();
        ssl_ctx.set_verify_mode(asio::ssl::verify_peer);
        asio::ssl::stream<asio::ip::tcp::socket> sock(ioc, ssl_ctx);
        sock.set_verify_callback(asio::ssl::host_name_verification(host));
        asio::ip::tcp::resolver resolver(ioc);
        asio::connect(sock.lowest_layer(), resolver.resolve(host, port_str));
        sock.handshake(asio::ssl::stream_base::client);

        std::string req = "GET " + path + " HTTP/1.0\r\nHost: " + host +
                          "\r\nConnection: close\r\n\r\n";
        asio::write(sock, asio::buffer(req));
        std::error_code ec;
        char buf[4096];
        while (true) {
            size_t n = sock.read_some(asio::buffer(buf), ec);
            if (n > 0) response.append(buf, n);
            if (ec) break;
        }
    } else {
        asio::ip::tcp::resolver resolver(ioc);
        asio::ip::tcp::socket sock(ioc);
        asio::connect(sock, resolver.resolve(host, port_str));
        std::string req = "GET " + path + " HTTP/1.0\r\nHost: " + host +
                          "\r\nConnection: close\r\n\r\n";
        asio::write(sock, asio::buffer(req));
        std::error_code ec;
        char buf[4096];
        while (true) {
            size_t n = sock.read_some(asio::buffer(buf), ec);
            if (n > 0) response.append(buf, n);
            if (ec) break;
        }
    }

    auto body_start = response.find("\r\n\r\n");
    return body_start != std::string::npos
           ? response.substr(body_start + 4) : response;
}

} // namespace novaMP::beammp
