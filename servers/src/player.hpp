// servers/src/player.hpp
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include "network/packet.hpp"

namespace novaMP {

enum class PlayerState {
    CONNECTING,
    LOADING_MODS,
    LOADING_MAP,
    READY,
    DISCONNECTING,
};

struct PlayerVehicle {
    uint8_t      vehicle_id;
    std::string  model;
    std::string  config;
    VehicleState last_state{};
    bool         spawned = false;
};

struct Player {
    uint16_t    id;
    std::string username;
    std::string role;
    std::string ip;
    uint16_t    remote_port = 0;
    PlayerState state       = PlayerState::CONNECTING;
    bool        is_muted    = false;
    bool        is_banned   = false;

    std::vector<PlayerVehicle> vehicles;
    int                        current_vehicle = -1;

    std::chrono::steady_clock::time_point last_packet_time;
    std::chrono::steady_clock::time_point connect_time;

    uint32_t udp_sequence = 0;
    uint32_t tcp_sequence = 0;

    PlayerVehicle* getVehicle(uint8_t vid) {
        for (auto& v : vehicles)
            if (v.vehicle_id == vid) return &v;
        return nullptr;
    }
};

} // namespace novaMP
