// client/src/launcher.cpp
#include "launcher.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#ifdef _WIN32
#  include <windows.h>
#  include <shlobj.h>
#else
#  include <unistd.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#endif

namespace fs = std::filesystem;
namespace novaMP {
using json = nlohmann::json;

Launcher::Launcher(const LaunchConfig& cfg) : m_cfg(cfg) {}

std::string Launcher::detectBeamNGPath() {
#ifdef _WIN32
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\BeamNG\\BeamNG.drive", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        char buf[MAX_PATH]; DWORD size = sizeof(buf);
        if (RegQueryValueExA(key, "InstallPath", nullptr, nullptr,
                             (LPBYTE)buf, &size) == ERROR_SUCCESS) {
            RegCloseKey(key);
            return std::string(buf) + "\\BeamNG.drive.exe";
        }
        RegCloseKey(key);
    }
    return "C:\\Program Files (x86)\\Steam\\steamapps\\common\\BeamNG.drive\\BeamNG.drive.exe";
#else
    const char* home = getenv("HOME");
    if (home) {
        std::string p = std::string(home) +
            "/.steam/steam/steamapps/common/BeamNG.drive/BinLinux64/BeamNG.drive.x64";
        if (fs::exists(p)) return p;
    }
    return "/usr/local/bin/BeamNG.drive";
#endif
}

std::string Launcher::beamngModsDir() const {
#ifdef _WIN32
    char docs[MAX_PATH];
    SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, docs);
    return std::string(docs) + "\\BeamNG.drive\\mods";
#else
    const char* home = getenv("HOME");
    return std::string(home ? home : ".") + "/.local/share/BeamNG.drive/mods";
#endif
}

bool Launcher::writeConnectConfig(const std::string& host, uint16_t port) {
    auto mods_dir   = beamngModsDir();
    auto config_dir = fs::path(mods_dir).parent_path().string();
    json j = {
        {"host",       host},
        {"port",       port},
        {"username",   m_cfg.username},
        {"token",      m_jwt_token},
        {"master_url", m_cfg.master_url}
    };
    std::ofstream f(config_dir + "/novaMP_connect.json");
    if (!f.is_open()) {
        spdlog::error("Cannot write connect config to {}", config_dir);
        return false;
    }
    f << j.dump(2);
    spdlog::info("Connect config written.");
    return true;
}

bool Launcher::installMod(StatusCallback status) {
    auto mods_dir = beamngModsDir();
    fs::create_directories(mods_dir);
    auto dest = mods_dir + "/novaMP.zip";

    if (status) status("Installing NovaMP mod...");
    try {
        fs::copy_file(m_cfg.novaMP_mod_path, dest,
                      fs::copy_options::overwrite_existing);
        spdlog::info("Mod installed: {}", dest);
        if (status) status("Mod installed.");
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Mod install failed: {}", e.what());
        if (status) status(std::string("Mod install failed: ") + e.what());
        return false;
    }
}

bool Launcher::launchGame(const std::string& server_host, uint16_t server_port,
                           StatusCallback status)
{
    if (!writeConnectConfig(server_host, server_port)) return false;

    std::string exe = m_cfg.beamng_path.empty()
                      ? detectBeamNGPath() : m_cfg.beamng_path;

    if (!fs::exists(exe)) {
        spdlog::error("BeamNG.drive not found: {}", exe);
        if (status) status("BeamNG.drive not found: " + exe);
        return false;
    }

    if (status) status("Launching BeamNG.drive...");
    spdlog::info("Launching: {}", exe);

#ifdef _WIN32
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    std::string cmd = "\"" + exe + "\"";
    if (!CreateProcessA(nullptr, (LPSTR)cmd.c_str(),
                        nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        spdlog::error("CreateProcess failed: {}", GetLastError());
        return false;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
#else
    pid_t pid = fork();
    if (pid == 0) {
        execl(exe.c_str(), exe.c_str(), nullptr);
        _exit(1);
    }
    if (pid < 0) { spdlog::error("fork() failed"); return false; }
#endif
    return true;
}

std::string Launcher::authenticate() {
    spdlog::info("Authenticating as '{}'...", m_cfg.username);
    // Full implementation POSTs to m_cfg.master_url + "/auth/login"
    // Returns JWT token; guest mode returns empty string.
    return "";
}

} // namespace novaMP
