// servers/src/lua/lua_engine.cpp
#include "lua_engine.hpp"
#include "../game_server.hpp"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;
namespace novaMP {

static GameServer* getServer(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "_novaMP_server");
    auto* s = static_cast<GameServer*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return s;
}

LuaEngine::LuaEngine(GameServer& server, const std::string& resources_dir)
    : m_server(server), m_resources_dir(resources_dir)
{
    m_L = luaL_newstate();
    if (!m_L) throw std::runtime_error("Failed to create Lua state");
    luaL_openlibs(m_L);

    lua_pushlightuserdata(m_L, &m_server);
    lua_setfield(m_L, LUA_REGISTRYINDEX, "_novaMP_server");

    registerAPI();
    spdlog::info("Lua engine initialized.");
}

LuaEngine::~LuaEngine() {
    if (m_L) lua_close(m_L);
}

void LuaEngine::registerAPI() {
    // ── MP table (BeamMP-compatible) ─────────────────────────────────────────
    lua_newtable(m_L);
    auto reg = [&](const char* name, lua_CFunction fn) {
        lua_pushcfunction(m_L, fn);
        lua_setfield(m_L, -2, name);
    };
    reg("SendChatMessage", lua_SendChatMessage);
    reg("KickPlayer",      lua_KickPlayer);
    reg("BanPlayer",       lua_BanPlayer);
    reg("GetPlayerName",   lua_GetPlayerName);
    reg("GetPlayerCount",  lua_GetPlayerCount);
    reg("GetPlayers",      lua_GetPlayers);
    reg("TriggerEvent",    lua_TriggerEvent);
    lua_setglobal(m_L, "MP");

    // ── NovaMP table (extended) ──────────────────────────────────────────────
    lua_newtable(m_L);

    lua_newtable(m_L); // NovaMP.AI
    auto regAI = [&](const char* name, lua_CFunction fn) {
        lua_pushcfunction(m_L, fn);
        lua_setfield(m_L, -2, name);
    };
    regAI("spawn",         lua_AISpawn);
    regAI("despawn",       lua_AIDespawn);
    regAI("despawnAll",    [](lua_State* L){ getServer(L)->aiTraffic().despawnAll(); return 0; });
    regAI("setCount",      lua_AISetCount);
    regAI("setMode",       lua_AISetMode);
    regAI("setSpeedLimit", lua_AISetSpeedLimit);
    regAI("status",        lua_AIStatus);
    lua_setfield(m_L, -2, "AI");

    lua_setglobal(m_L, "NovaMP");

    // Override print
    lua_pushcfunction(m_L, lua_print);
    lua_setglobal(m_L, "print");
}

void LuaEngine::loadPlugins() {
    std::string dir = m_resources_dir + "/plugins";
    if (!fs::exists(dir)) {
        spdlog::warn("Lua: Plugin dir not found: {}", dir);
        return;
    }
    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".lua")
            loadFile(entry.path().string());
    }
}

void LuaEngine::loadFile(const std::string& path) {
    spdlog::info("Lua: Loading {}", path);
    if (luaL_dofile(m_L, path.c_str()) != LUA_OK) {
        spdlog::error("Lua error in {}: {}", path, lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
    }
}

std::string LuaEngine::fireEvent(const std::string& event,
                                  const std::vector<std::string>& args)
{
    lua_getglobal(m_L, event.c_str());
    if (!lua_isfunction(m_L, -1)) { lua_pop(m_L, 1); return ""; }
    for (auto& a : args) lua_pushstring(m_L, a.c_str());
    if (lua_pcall(m_L, (int)args.size(), 1, 0) != LUA_OK) {
        spdlog::error("Lua event '{}' error: {}", event, lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
        return "";
    }
    std::string ret;
    if (lua_isstring(m_L, -1)) ret = lua_tostring(m_L, -1);
    lua_pop(m_L, 1);
    return ret;
}

std::string LuaEngine::exec(const std::string& code) {
    if (luaL_dostring(m_L, code.c_str()) != LUA_OK) {
        std::string err = lua_tostring(m_L, -1);
        lua_pop(m_L, 1);
        return "Error: " + err;
    }
    return "OK";
}

// ── API implementations ───────────────────────────────────────────────────────
int LuaEngine::lua_print(lua_State* L) {
    std::string msg;
    int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        if (i > 1) msg += "\t";
        msg += luaL_tolstring(L, i, nullptr);
        lua_pop(L, 1);
    }
    spdlog::info("[Lua] {}", msg);
    return 0;
}

int LuaEngine::lua_SendChatMessage(lua_State* L) {
    int  pid = (int)luaL_checkinteger(L, 1);
    auto msg = luaL_checkstring(L, 2);
    getServer(L)->sendChat(pid, std::string("[Server] ") + msg);
    return 0;
}

int LuaEngine::lua_KickPlayer(lua_State* L) {
    int  pid    = (int)luaL_checkinteger(L, 1);
    auto reason = lua_isstring(L, 2) ? lua_tostring(L, 2) : "Kicked";
    getServer(L)->kickPlayer(pid, reason);
    return 0;
}

int LuaEngine::lua_BanPlayer(lua_State* L) {
    int  pid    = (int)luaL_checkinteger(L, 1);
    auto reason = lua_isstring(L, 2) ? lua_tostring(L, 2) : "Banned";
    getServer(L)->banPlayer(pid, reason);
    return 0;
}

int LuaEngine::lua_GetPlayerName(lua_State* L) {
    int pid = (int)luaL_checkinteger(L, 1);
    auto* p = getServer(L)->getPlayer(pid);
    lua_pushstring(L, p ? p->username.c_str() : "");
    return 1;
}

int LuaEngine::lua_GetPlayerCount(lua_State* L) {
    lua_pushinteger(L, getServer(L)->playerCount());
    return 1;
}

int LuaEngine::lua_GetPlayers(lua_State* L) {
    lua_newtable(L);
    int i = 1;
    for (auto& [id, p] : getServer(L)->players()) {
        lua_newtable(L);
        lua_pushinteger(L, p->id);             lua_setfield(L, -2, "id");
        lua_pushstring(L, p->username.c_str()); lua_setfield(L, -2, "name");
        lua_pushstring(L, p->role.c_str());     lua_setfield(L, -2, "role");
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

int LuaEngine::lua_TriggerEvent(lua_State* L) {
    auto event = luaL_checkstring(L, 1);
    auto data  = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";
    getServer(L)->lua().fireEvent(event, {data});
    return 0;
}

int LuaEngine::lua_AISpawn(lua_State* L) {
    std::string model = "etk800";
    float x = 0, y = 0, z = 0;
    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "model"); if (lua_isstring(L,-1)) model = lua_tostring(L,-1); lua_pop(L,1);
        lua_getfield(L, 1, "x"); x = (float)lua_tonumber(L,-1); lua_pop(L,1);
        lua_getfield(L, 1, "y"); y = (float)lua_tonumber(L,-1); lua_pop(L,1);
        lua_getfield(L, 1, "z"); z = (float)lua_tonumber(L,-1); lua_pop(L,1);
    } else {
        model = luaL_optstring(L, 1, "etk800");
        x = (float)luaL_optnumber(L, 2, 0);
        y = (float)luaL_optnumber(L, 3, 0);
        z = (float)luaL_optnumber(L, 4, 0);
    }
    getServer(L)->aiTraffic().spawnVehicle(model, x, y, z);
    return 0;
}

int LuaEngine::lua_AIDespawn(lua_State* L) {
    getServer(L)->aiTraffic().despawnVehicle((uint8_t)luaL_checkinteger(L, 1));
    return 0;
}

int LuaEngine::lua_AISetCount(lua_State* L) {
    getServer(L)->aiTraffic().setCount((int)luaL_checkinteger(L, 1));
    return 0;
}

int LuaEngine::lua_AISetMode(lua_State* L) {
    getServer(L)->aiTraffic().setMode(luaL_checkstring(L, 1));
    return 0;
}

int LuaEngine::lua_AISetSpeedLimit(lua_State* L) {
    getServer(L)->aiTraffic().setSpeedLimit((float)luaL_checknumber(L, 1));
    return 0;
}

int LuaEngine::lua_AIStatus(lua_State* L) {
    int cnt = getServer(L)->aiTraffic().activeCount();
    spdlog::info("AI Status: {} active vehicles", cnt);
    lua_pushinteger(L, cnt);
    return 1;
}

} // namespace novaMP
