// client/src/compat/beammp_protocol.cpp
#include "beammp_protocol.hpp"
#include <sstream>
#include <vector>
#include <charconv>

namespace novaMP::beammp {

// Parse "<pid>-<vid>:<px>:<py>:<pz>:<rx>:<ry>:<rz>:<rw>:<vx>:<vy>:<vz>:<ax>:<ay>:<az>:<ts>"
bool parseTransform(const std::string& data, BeamMPTransform& out) {
    // Split on ':'
    std::vector<std::string> parts;
    std::istringstream ss(data);
    std::string tok;
    while (std::getline(ss, tok, ':')) parts.push_back(tok);
    if (parts.size() < 14) return false;

    // First part: "<pid>-<vid>"
    auto dash = parts[0].find('-');
    if (dash == std::string::npos) return false;
    try {
        out.player_id  = (uint16_t)std::stoi(parts[0].substr(0, dash));
        out.vehicle_id = (uint8_t) std::stoi(parts[0].substr(dash + 1));
        out.pos[0]     = std::stof(parts[1]);
        out.pos[1]     = std::stof(parts[2]);
        out.pos[2]     = std::stof(parts[3]);
        out.rot[0]     = std::stof(parts[4]);
        out.rot[1]     = std::stof(parts[5]);
        out.rot[2]     = std::stof(parts[6]);
        out.rot[3]     = std::stof(parts[7]);
        out.vel[0]     = std::stof(parts[8]);
        out.vel[1]     = std::stof(parts[9]);
        out.vel[2]     = std::stof(parts[10]);
        out.ang_vel[0] = std::stof(parts[11]);
        out.ang_vel[1] = std::stof(parts[12]);
        out.ang_vel[2] = std::stof(parts[13]);
        if (parts.size() > 14) out.timestamp = std::stod(parts[14]);
    } catch (...) { return false; }
    return true;
}

std::string encodeTransform(uint16_t pid, uint8_t vid,
                             const float pos[3], const float rot[4],
                             const float vel[3], const float ang_vel[3])
{
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "%d-%d:%.6f:%.6f:%.6f:%.6f:%.6f:%.6f:%.6f:%.6f:%.6f:%.6f:%.6f:%.6f:%.6f:0",
        (int)pid, (int)vid,
        pos[0], pos[1], pos[2],
        rot[0], rot[1], rot[2], rot[3],
        vel[0], vel[1], vel[2],
        ang_vel[0], ang_vel[1], ang_vel[2]);
    return buf;
}

} // namespace novaMP::beammp
