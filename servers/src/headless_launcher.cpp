// servers/src/headless_launcher.cpp
#include "headless_launcher.hpp"
#include "config/config.hpp"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <stdexcept>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <shlobj.h>
#else
#  include <unistd.h>
#  include <csignal>
#  include <sys/types.h>
#  include <sys/wait.h>
#endif

namespace novaMP {
namespace fs = std::filesystem;

HeadlessLauncher::HeadlessLauncher(const ServerConfig& cfg) : m_cfg(cfg) {}

// ── Path detection ────────────────────────────────────────────────────────────

std::string HeadlessLauncher::detectBeamNGPath() const {
    if (!m_cfg.beamng_exe_path.empty()) {
        if (fs::exists(m_cfg.beamng_exe_path)) return m_cfg.beamng_exe_path;
        spdlog::warn("[Headless] Configured exe path not found: {}", m_cfg.beamng_exe_path);
    }
#ifdef _WIN32
    return detectWindows();
#else
    return detectLinux();
#endif
}

#ifdef _WIN32
std::string HeadlessLauncher::detectWindows() const {
    // 1. Steam registry key (most common install)
    const char* steam_key =
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
        "Steam App 284160";
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, steam_key, 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        char buf[MAX_PATH];
        DWORD sz = sizeof(buf);
        if (RegQueryValueExA(hkey, "InstallLocation", nullptr, nullptr,
                             (LPBYTE)buf, &sz) == ERROR_SUCCESS) {
            RegCloseKey(hkey);
            std::string path = std::string(buf) + "\\BeamNG.drive.exe";
            if (fs::exists(path)) return path;
        }
        RegCloseKey(hkey);
    }

    // 2. Common non-Steam locations
    const char* candidates[] = {
        "C:\\Program Files (x86)\\Steam\\steamapps\\common\\BeamNG.drive\\BeamNG.drive.exe",
        "C:\\Program Files\\Steam\\steamapps\\common\\BeamNG.drive\\BeamNG.drive.exe",
        "D:\\SteamLibrary\\steamapps\\common\\BeamNG.drive\\BeamNG.drive.exe",
    };
    for (auto* p : candidates)
        if (fs::exists(p)) return p;

    return "";
}
#else
std::string HeadlessLauncher::detectLinux() const {
    // Common Steam on Linux paths
    const char* candidates[] = {
        "/home/user/.steam/steam/steamapps/common/BeamNG.drive/BeamNG.drive.x86_64",
        "/opt/steam/steamapps/common/BeamNG.drive/BeamNG.drive.x86_64",
    };
    // Check $HOME too
    const char* home = std::getenv("HOME");
    std::string home_path;
    if (home) {
        home_path = std::string(home) +
                    "/.steam/steam/steamapps/common/BeamNG.drive/BeamNG.drive.x86_64";
    }

    for (auto* p : candidates)
        if (fs::exists(p)) return p;
    if (!home_path.empty() && fs::exists(home_path)) return home_path;

    spdlog::warn("[Headless] BeamNG not found on Linux. "
                 "Set beamng_exe_path in [ai_authority] config.");
    return "";
}
#endif

// ── Launch ────────────────────────────────────────────────────────────────────

bool HeadlessLauncher::launch(const std::function<void(const std::string&)>& progress) {
    m_exe_path = detectBeamNGPath();
    if (m_exe_path.empty()) {
        progress("BeamNG.drive not found — cannot launch headless instance.");
        return false;
    }
    progress("Found BeamNG.drive at: " + m_exe_path);

    // The bridge mod must be in BeamNG's mods directory.
    // We tell BeamNG to connect to localhost so the bridge Lua can reach us.
    // -tcom-listen-ip and -userpath are BeamNG launch flags.
    // -window 0x0 makes it truly invisible on Windows.

#ifdef _WIN32
    std::string cmdline = "\"" + m_exe_path + "\""
        " -nosteam"
        " -window 0x0"
        " -console"
        " -tcom-listen-ip 127.0.0.1"
        " -nolegality";

    STARTUPINFOA si{};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(
            nullptr,
            const_cast<char*>(cmdline.c_str()),
            nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW,
            nullptr, nullptr, &si, &pi))
    {
        progress("CreateProcess failed for headless BeamNG.");
        return false;
    }
    m_process_handle = pi.hProcess;
    CloseHandle(pi.hThread);
    progress("Headless BeamNG launched (PID=" + std::to_string(pi.dwProcessId) + ").");
    return true;

#else
    // On Linux: launch via fork/exec. Requires Xvfb or a virtual framebuffer
    // (BeamNG is not truly headless on Linux yet).
    pid_t pid = fork();
    if (pid < 0) {
        progress("fork() failed — cannot launch headless BeamNG.");
        return false;
    }
    if (pid == 0) {
        // Child
        execl(m_exe_path.c_str(), m_exe_path.c_str(),
              "-nosteam", "-console", "-tcom-listen-ip", "127.0.0.1",
              (char*)nullptr);
        _exit(1);
    }
    m_pid = pid;
    progress("Headless BeamNG launched (PID=" + std::to_string(m_pid) + ").");
    return true;
#endif
}

void HeadlessLauncher::terminate() {
#ifdef _WIN32
    if (m_process_handle) {
        TerminateProcess(m_process_handle, 0);
        CloseHandle(m_process_handle);
        m_process_handle = nullptr;
    }
#else
    if (m_pid > 0) {
        kill(m_pid, SIGTERM);
        waitpid(m_pid, nullptr, 0);
        m_pid = -1;
    }
#endif
}

bool HeadlessLauncher::isRunning() const {
#ifdef _WIN32
    if (!m_process_handle) return false;
    DWORD code;
    return GetExitCodeProcess(m_process_handle, &code) && code == STILL_ACTIVE;
#else
    if (m_pid <= 0) return false;
    return waitpid(m_pid, nullptr, WNOHANG) == 0;
#endif
}

} // namespace novaMP
