// servers/src/network/packet.hpp
// NovaMP Network Protocol — Binary packet definitions
//
// Every packet starts with a 10-byte header:
//   [1] type  [2] sender_id  [4] sequence  [2] payload_len  [1] flags
//
// TCP packets are prefixed with a 4-byte little-endian frame length.
// UDP packets are a single datagram (no framing prefix).
//
// Vehicle Update payload (66 bytes):
//   [1] vehicle_id  [1] vflags  [12] pos  [16] rot  [12] vel
//   [12] ang_vel    [4] wheels  [1] lights [1] gear  [4] rpm  [2] health

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
    READY=0x17, KICK=0x18, BAN=0x19,
    // AI authority negotiation
    AUTHORITY_CLAIM=0x20,   // Client → Server  (volunteer to drive AI)
    AUTHORITY_GRANT=0x21,   // Server → Client  (you are now the AI authority)
    AUTHORITY_REVOKE=0x22,  // Server → Client  (authority taken away)
    ERROR=0xFF,
};

enum PacketFlags  : uint8_t { FLAG_NONE=0, FLAG_RELIABLE=1, FLAG_COMPRESSED=2 };
enum VehicleFlags : uint8_t { VF_NONE=0, VF_IS_AI=1, VF_SLEEPING=2 };
enum LightFlags   : uint8_t {
    LF_HEADLIGHTS=0x01, LF_BRAKE=0x02, LF_REVERSE=0x04,
    LF_SIGNAL_LEFT=0x08, LF_SIGNAL_RIGHT=0x10,
    LF_HAZARD=0x18, LF_BEACONS=0x20
};

#pragma pack(push, 1)
struct PacketHeader {
    uint8_t  type;
    uint16_t sender_id;
    uint32_t sequence;
    uint16_t payload_len;
    uint8_t  flags;
};
static_assert(sizeof(PacketHeader) == 10, "PacketHeader must be 10 bytes");

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
static_assert(sizeof(VehicleState) == 66, "VehicleState must be 66 bytes");
#pragma pack(pop)

class Packet {
public:
    static constexpr size_t HEADER_SIZE = sizeof(PacketHeader);

    static std::vector<uint8_t> build(PacketType type, uint16_t sender_id,
                                       uint32_t sequence, const void* payload,
                                       uint16_t payload_len, uint8_t flags = 0)
    {
        std::vector<uint8_t> buf(HEADER_SIZE + payload_len);
        PacketHeader hdr{(uint8_t)type, sender_id, sequence, payload_len, flags};
        std::memcpy(buf.data(), &hdr, HEADER_SIZE);
        if (payload_len && payload)
            std::memcpy(buf.data() + HEADER_SIZE, payload, payload_len);
        return buf;
    }

    static std::vector<uint8_t> buildStr(PacketType type, uint16_t sender_id,
                                          uint32_t sequence, const std::string& payload)
    {
        return build(type, sender_id, sequence,
                     payload.data(), (uint16_t)payload.size());
    }

    static std::vector<uint8_t> buildVehicleUpdate(uint16_t sender_id,
                                                    uint32_t sequence,
                                                    const VehicleState& vs)
    {
        return build(PacketType::VEHICLE_UPDATE, sender_id, sequence,
                     &vs, (uint16_t)sizeof(vs));
    }

    static PacketHeader parseHeader(const uint8_t* data, size_t len) {
        if (len < HEADER_SIZE) throw std::runtime_error("Packet too short");
        PacketHeader hdr;
        std::memcpy(&hdr, data, HEADER_SIZE);
        return hdr;
    }

    static const uint8_t* payload(const uint8_t* data) {
        return data + HEADER_SIZE;
    }

    static VehicleState parseVehicleState(const uint8_t* payload, uint16_t len) {
        if (len < sizeof(VehicleState)) throw std::runtime_error("VehicleState payload too short");
        VehicleState vs;
        std::memcpy(&vs, payload, sizeof(vs));
        return vs;
    }
};

} // namespace novaMP
