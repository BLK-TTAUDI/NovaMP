// servers/src/lua/lua_engine.hpp
// Embedded Lua 5.3 plugin engine — BeamMP-compatible event API
#pragma once

#include <string>
#include <vector>
#include <memory>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace novaMP {

class GameServer;

class LuaEngine {
public:
    explicit LuaEngine(GameServer& server, const std::string& resources_dir);
    ~LuaEngine();

    void loadPlugins();

    // Fire a named event; returns the plugin's return value as string (or "")
    std::string fireEvent(const std::string& event,
                          const std::vector<std::string>& args = {});

    // Execute raw Lua code (for console / RCON)
    std::string exec(const std::string& code);

private:
    lua_State*  m_L;
    GameServer& m_server;
    std::string m_resources_dir;

    void registerAPI();
    void loadFile(const std::string& path);

    // MP.* API
    static int lua_SendChatMessage (lua_State* L);
    static int lua_KickPlayer      (lua_State* L);
    static int lua_BanPlayer       (lua_State* L);
    static int lua_GetPlayerName   (lua_State* L);
    static int lua_GetPlayerCount  (lua_State* L);
    static int lua_GetPlayers      (lua_State* L);
    static int lua_TriggerEvent    (lua_State* L);
    static int lua_print           (lua_State* L);

    // NovaMP.AI.* API
    static int lua_AISpawn         (lua_State* L);
    static int lua_AIDespawn       (lua_State* L);
    static int lua_AISetCount      (lua_State* L);
    static int lua_AISetMode       (lua_State* L);
    static int lua_AISetSpeedLimit (lua_State* L);
    static int lua_AIStatus        (lua_State* L);
};

} // namespace novaMP
