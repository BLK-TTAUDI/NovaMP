// client/src/ui/server_browser.hpp
#pragma once
#include <string>
#include <vector>
#include <functional>

namespace novaMP {

enum class ServerNetwork {
    NOVAMP,   // NovaMP dedicated server — use NovaMP protocol
    BEAMMP,   // BeamMP game server    — use BeamMP protocol
};

struct ServerEntry {
    int64_t       id = 0;
    std::string   name, description, host, map, version;
    int           port            = 4444;
    int           current_players = 0;
    int           max_players     = 0;
    bool          password_protected = false;
    bool          modded          = false;
    ServerNetwork network         = ServerNetwork::NOVAMP;
};

class ServerBrowser {
public:
    using SelectCallback = std::function<void(const ServerEntry&)>;

    // master_url  — NovaMP master server base URL
    // fetch_beammp — also fetch BeamMP's public server list
    explicit ServerBrowser(const std::string& master_url,
                           bool fetch_beammp = true);

    // Fetch both server lists (blocking). Returns false if both fail.
    bool refresh();

    // Interactive console UI. Presents a combined, filterable list.
    void runConsoleUI(SelectCallback on_select);

    const std::vector<ServerEntry>& servers() const { return m_servers; }

private:
    std::string              m_master_url;
    bool                     m_fetch_beammp;
    std::vector<ServerEntry> m_servers;

    bool fetchNovaMP();
    bool fetchBeamMP();
};

} // namespace novaMP
