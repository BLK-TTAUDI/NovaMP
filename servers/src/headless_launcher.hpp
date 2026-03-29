// servers/src/headless_launcher.hpp
//
// HeadlessLauncher — detects a local BeamNG.drive installation and, if found,
// launches it in a windowless mode so it can run the ai_bridge Lua mod.
//
// BeamNG.drive supports running without a display via the -nosteam and
// -tcom-listen-ip flags combined with -window 0x0 (Windows) or via
// the DISPLAY="" trick on Linux with Xvfb.
//
// The bridge mod (Resources/Server/ai_bridge/main.lua) will then connect
// back to this server's TCP port using the bridge_token for authentication.

#pragma once
#include <string>
#include <functional>

namespace novaMP {

struct ServerConfig;

class HeadlessLauncher {
public:
    explicit HeadlessLauncher(const ServerConfig& cfg);

    // Returns the discovered executable path, or "" if not found.
    std::string detectBeamNGPath() const;

    // Launch BeamNG headlessly, injecting the ai_bridge mod.
    // progress_fn receives status strings during launch.
    // Returns true if the process was started.
    bool launch(const std::function<void(const std::string&)>& progress_fn);

    // Terminate the headless process if we started it.
    void terminate();

    bool isRunning() const;

private:
    const ServerConfig& m_cfg;

#ifdef _WIN32
    void* m_process_handle = nullptr;  // HANDLE
#else
    int   m_pid = -1;
#endif

    std::string m_exe_path;

    std::string detectWindows() const;
    std::string detectLinux()   const;
};

} // namespace novaMP
