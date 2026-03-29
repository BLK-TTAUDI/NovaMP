// client/src/compat/beammp_protocol.hpp
//
// BeamMP wire protocol constants.
//
// Based on BeamMP-Server and BeamMP-Launcher (AGPL-3.0,
// https://github.com/BeamMP/BeamMP-Server).
//
// Frame format (TCP):
//   [code: 1 byte][size: 3 bytes little-endian][data: size bytes]
//
// Large payloads that exceed ~100 KB are split by the sender with code 'S'
// (split) and reassembled before delivery.  The final split chunk is marked
// with code 'F' (finish).  Split reassembly is handled in BeamMPClient.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace novaMP::beammp {

// ── Packet codes (single ASCII character used as the packet type byte) ────────
enum Code : uint8_t {
    // Client ↔ Server
    CODE_AUTH_PLAYER   = 'P',  // client sends player key; server replies with player ID
    CODE_AUTH_KICK     = 'K',  // server kicks client (with reason)
    CODE_CHAT          = 'C',  // chat message (broadcast or directed)
    CODE_DISCONNECT    = 'D',  // graceful disconnect notification
    CODE_MAP           = 'M',  // server → client: map name; client → server: map loaded
    CODE_OK            = 'O',  // generic acknowledgement
    CODE_PING          = 'p',  // ping request
    CODE_PONG          = 'q',  // ping response
    CODE_READY         = 'R',  // client signals ready-to-play
    CODE_SYNC          = 'T',  // vehicle transform update (colon-separated string)
    CODE_VEHICLE_SPAWN = 'V',  // vehicle spawn data
    CODE_VEHICLE_DEL   = 'X',  // delete a vehicle
    CODE_VEHICLE_RESET = 'Z',  // reset vehicle to spawn position
    CODE_ELECTRICS     = 'E',  // vehicle electrics/lights update
    CODE_NODE_MOVED    = 'N',  // individual node position (used for soft-body sync)
    CODE_SPLIT         = 'S',  // split (multi-chunk) packet start/middle
    CODE_SPLIT_FINISH  = 'F',  // split packet last chunk
    CODE_SERVER_INFO   = 'I',  // server → client: server info JSON
    CODE_WAIT          = 'W',  // server tells client to wait (loading)
};

// ── Frame helpers ─────────────────────────────────────────────────────────────

// Build a BeamMP framed packet.
inline std::vector<uint8_t> buildPacket(Code code, const std::string& data) {
    uint32_t sz  = (uint32_t)data.size();
    std::vector<uint8_t> pkt(4 + sz);
    pkt[0] = (uint8_t)code;
    pkt[1] = (uint8_t)( sz        & 0xFF);
    pkt[2] = (uint8_t)((sz >>  8) & 0xFF);
    pkt[3] = (uint8_t)((sz >> 16) & 0xFF);
    std::memcpy(pkt.data() + 4, data.data(), sz);
    return pkt;
}

inline std::vector<uint8_t> buildPacket(Code code, const std::vector<uint8_t>& data) {
    return buildPacket(code, std::string(data.begin(), data.end()));
}

// Parse frame size from the first 4 bytes of a received buffer.
// Returns 0 if the buffer has fewer than 4 bytes.
inline uint32_t parseFrameSize(const uint8_t* buf, size_t avail) {
    if (avail < 4) return 0;
    return (uint32_t)buf[1] | ((uint32_t)buf[2] << 8) | ((uint32_t)buf[3] << 16);
}

// ── Vehicle transform string format ──────────────────────────────────────────
// BeamMP encodes vehicle positions as a colon-separated ASCII string:
//   "<pid>-<vid>:<pos_x>:<pos_y>:<pos_z>:<rot_x>:<rot_y>:<rot_z>:<rot_w>:
//    <vel_x>:<vel_y>:<vel_z>:<avel_x>:<avel_y>:<avel_z>:<time>"
// We parse this into our VehicleState for display in BeamNG.
struct BeamMPTransform {
    uint16_t player_id  = 0;
    uint8_t  vehicle_id = 0;
    float    pos[3]     = {};
    float    rot[4]     = {0,0,0,1};
    float    vel[3]     = {};
    float    ang_vel[3] = {};
    double   timestamp  = 0;
};

bool parseTransform(const std::string& data, BeamMPTransform& out);
std::string encodeTransform(uint16_t pid, uint8_t vid,
                            const float pos[3], const float rot[4],
                            const float vel[3], const float ang_vel[3]);

} // namespace novaMP::beammp
