// client/src/launcher.hpp
#pragma once
#include <string>
#include <functional>

namespace novaMP {

struct LaunchConfig {
    std::string beamng_path;
    std::string novaMP_mod_path;
    std::string master_url;
    std::string username;
    std::string password;
};

class Launcher {
public:
    using StatusCallback = std::function<void(const std::string&)>;

    explicit Launcher(const LaunchConfig& cfg);

    static std::string detectBeamNGPath();

    bool installMod(StatusCallback status);
    bool launchGame(const std::string& server_host, uint16_t server_port,
                    StatusCallback status);
    std::string authenticate();

private:
    LaunchConfig m_cfg;
    std::string  m_jwt_token;

    std::string beamngModsDir() const;
    bool        writeConnectConfig(const std::string& host, uint16_t port);
};

} // namespace novaMP
