// client/src/network/packet.hpp
// Identical protocol definition to servers/src/network/packet.hpp
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <stdexcept>

namespace novaMP {

enum class PacketType : uint8_t {
    HEARTBEAT=0x00, AUTH_REQUEST=0x01, AUTH_RESPONSE=0x02,
    PLAYER_JOIN=0x03, PLAYER_LEAVE=0x04, PLAYER_INFO=0x05,
    VEHICLE_SPAWN=0x06, VEHICLE_DELETE=0x07, VEHICLE_UPDATE=0x08,
    VEHICLE_DAMAGE=0x09, VEHICLE_ELECTRICS=0x0A, CHAT_MESSAGE=0x0B,
    SERVER_INFO=0x0C, MOD_LIST=0x0D, MOD_DATA=0x0E, MOD_OK=0x0F,
    PING=0x10, PONG=0x11, RCON_AUTH=0x12, RCON_COMMAND=0x13,
    RCON_RESPONSE=0x14, SERVER_EVENT=0x15, PLAYER_TELEPORT=0x16,
    READY=0x17, KICK=0x18, BAN=0x19, ERROR=0xFF,
};

enum PacketFlags  : uint8_t { FLAG_NONE=0, FLAG_RELIABLE=1, FLAG_COMPRESSED=2 };
enum VehicleFlags : uint8_t { VF_NONE=0, VF_IS_AI=1, VF_SLEEPING=2 };
enum LightFlags   : uint8_t {
    LF_HEADLIGHTS=0x01, LF_BRAKE=0x02, LF_REVERSE=0x04,
    LF_SIGNAL_LEFT=0x08, LF_SIGNAL_RIGHT=0x10,
    LF_HAZARD=0x18, LF_BEACONS=0x20
};

#pragma pack(push,1)
struct PacketHeader {
    uint8_t  type;
    uint16_t sender_id;
    uint32_t sequence;
    uint16_t payload_len;
    uint8_t  flags;
};
struct VehicleState {
    uint8_t  vehicle_id;
    uint8_t  vflags;
    float    pos[3];
    float    rot[4];
    float    vel[3];
    float    ang_vel[3];
    uint8_t  wheels[4];
    uint8_t  lights;
    int8_t   gear;
    float    rpm;
    uint16_t health;
};
#pragma pack(pop)

class Packet {
public:
    static constexpr size_t HEADER_SIZE = sizeof(PacketHeader);

    static std::vector<uint8_t> build(PacketType t, uint16_t sid, uint32_t seq,
                                       const void* p, uint16_t plen, uint8_t f=0) {
        std::vector<uint8_t> buf(HEADER_SIZE + plen);
        PacketHeader hdr{(uint8_t)t, sid, seq, plen, f};
        memcpy(buf.data(), &hdr, HEADER_SIZE);
        if (plen && p) memcpy(buf.data() + HEADER_SIZE, p, plen);
        return buf;
    }

    static std::vector<uint8_t> buildStr(PacketType t, uint16_t sid, uint32_t seq,
                                          const std::string& s) {
        return build(t, sid, seq, s.data(), (uint16_t)s.size());
    }

    static std::vector<uint8_t> buildVehicleUpdate(uint16_t sid, uint32_t seq,
                                                    const VehicleState& vs) {
        return build(PacketType::VEHICLE_UPDATE, sid, seq,
                     &vs, (uint16_t)sizeof(vs));
    }

    static PacketHeader parseHeader(const uint8_t* d, size_t n) {
        if (n < HEADER_SIZE) throw std::runtime_error("too short");
        PacketHeader h; memcpy(&h, d, HEADER_SIZE); return h;
    }

    static const uint8_t* payload(const uint8_t* d) { return d + HEADER_SIZE; }
};

} // namespace novaMP
