// servers/src/sync/vehicle_sync.cpp
#include "vehicle_sync.hpp"
#include <algorithm>

namespace novaMP {

VehicleSync::VehicleSync(int sync_hz, BroadcastFn broadcast)
    : m_sync_hz(sync_hz), m_broadcast(std::move(broadcast))
{}

VehicleSync::SyncEntry* VehicleSync::findOrCreate(uint16_t owner, uint8_t vehicle_id) {
    for (auto& e : m_entries)
        if (e.owner_id == owner && e.state.vehicle_id == vehicle_id)
            return &e;
    SyncEntry ne{};
    ne.owner_id         = owner;
    ne.state.vehicle_id = vehicle_id;
    m_entries.push_back(ne);
    return &m_entries.back();
}

void VehicleSync::onVehicleUpdate(uint16_t player_id, const VehicleState& state) {
    auto* e  = findOrCreate(player_id, state.vehicle_id);
    e->state = state;
    e->dirty = true;
}

void VehicleSync::broadcastAll() {
    for (auto& e : m_entries) {
        if (!e.dirty) continue;
        auto pkt = Packet::buildVehicleUpdate(e.owner_id, ++e.seq, e.state);
        m_broadcast(pkt, e.owner_id);
        e.dirty = false;
    }
}

void VehicleSync::onPlayerDisconnect(uint16_t player_id) {
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(),
            [player_id](const SyncEntry& e) { return e.owner_id == player_id; }),
        m_entries.end());
}

void VehicleSync::onVehicleDelete(uint16_t player_id, uint8_t vehicle_id) {
    remove(player_id, vehicle_id);
}

void VehicleSync::remove(uint16_t owner, uint8_t vehicle_id) {
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(),
            [owner, vehicle_id](const SyncEntry& e) {
                return e.owner_id == owner && e.state.vehicle_id == vehicle_id;
            }),
        m_entries.end());
}

} // namespace novaMP
