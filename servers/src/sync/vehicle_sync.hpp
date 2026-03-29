// servers/src/sync/vehicle_sync.hpp
#pragma once

#include <vector>
#include <functional>
#include <cstdint>
#include "../network/packet.hpp"

namespace novaMP {

// VehicleSync collects vehicle states from owner clients and broadcasts
// them to all other clients at the configured sync rate (default 100 Hz).
class VehicleSync {
public:
    using BroadcastFn = std::function<void(
        const std::vector<uint8_t>& data, uint16_t exclude_player_id)>;

    explicit VehicleSync(int sync_hz, BroadcastFn broadcast);

    void onVehicleUpdate(uint16_t player_id, const VehicleState& state);
    void broadcastAll();
    void onPlayerDisconnect(uint16_t player_id);
    void onVehicleDelete(uint16_t player_id, uint8_t vehicle_id);

    struct SyncEntry {
        uint16_t     owner_id;
        VehicleState state;
        uint32_t     seq   = 0;
        bool         dirty = false;
    };

    const std::vector<SyncEntry>& entries() const { return m_entries; }

private:
    int         m_sync_hz;
    BroadcastFn m_broadcast;
    std::vector<SyncEntry> m_entries;

    SyncEntry* findOrCreate(uint16_t owner, uint8_t vehicle_id);
    void       remove(uint16_t owner, uint8_t vehicle_id);
};

} // namespace novaMP
