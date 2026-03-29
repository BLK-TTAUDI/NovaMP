-- servers/Resources/Server/ai_bridge/main.lua
--
-- NovaMP AI Bridge — runs inside a headless BeamNG.drive instance on the server.
--
-- This mod connects back to the NovaMP game server as a special "client", then:
--   1. Spawns AI traffic vehicles using BeamNG's native aiManager.
--   2. At ai_hz, reads each AI vehicle's real physics state.
--   3. Forwards those states to the server as VEHICLE_UPDATE (0x08) packets
--      with VF_IS_AI set — exactly what the server expects.
--
-- Authentication: sends AUTH_REQUEST with username="##ai_bridge##" and
-- server_password = bridge_token from ServerConfig.toml [ai_authority].
-- The server recognises this username and calls onHeadlessConnect().
--
-- Config is read from /userdata/novaMP_bridge.json written by the C++ server.

local M = {}

-- ── Protocol constants ────────────────────────────────────────────────────────
local PKT_AUTH_REQUEST  = 0x01
local PKT_AUTH_RESPONSE = 0x02
local PKT_VEHICLE_UPDATE= 0x08
local PKT_CHAT_MESSAGE  = 0x0B
local PKT_SERVER_INFO   = 0x0C
local PKT_KICK          = 0x18
local VF_IS_AI          = 0x01

-- ── State ─────────────────────────────────────────────────────────────────────
local socket      = nil
local udp_socket  = nil
local recv_buf    = ""
local tcp_seq     = 0
local player_id   = 0
local connected   = false

-- AI vehicle tracking
local ai_vehicles = {}   -- game_vid -> { server_vid, spawned_time }
local next_ai_vid = 200  -- IDs 200-254 reserved for AI
local MAX_AI_VID  = 254

-- Config loaded from /userdata/novaMP_bridge.json
local cfg = {
    host         = "127.0.0.1",
    port         = 4444,
    bridge_token = "changeme_bridge",
    ai_count     = 10,
    ai_speed_limit = 14.0,
    ai_mode      = "traffic",
    ai_hz        = 20,
    vehicle_pool = {"etk800", "vivace", "pessima", "roamer"},
}

local send_interval = 1.0 / cfg.ai_hz
local send_timer    = 0.0

-- ── Helpers ───────────────────────────────────────────────────────────────────
local function loadConfig()
    local f = io.open("/userdata/novaMP_bridge.json", "r")
    if not f then return end
    local ok, j = pcall(jsonDecode, f:read("*a")); f:close()
    if not ok or not j then return end
    if j.host          then cfg.host          = j.host          end
    if j.port          then cfg.port          = j.port          end
    if j.bridge_token  then cfg.bridge_token  = j.bridge_token  end
    if j.ai_count      then cfg.ai_count      = j.ai_count      end
    if j.ai_speed_limit then cfg.ai_speed_limit = j.ai_speed_limit end
    if j.ai_mode       then cfg.ai_mode       = j.ai_mode       end
    if j.ai_hz         then cfg.ai_hz         = j.ai_hz         end
    if j.vehicle_pool  then cfg.vehicle_pool  = j.vehicle_pool  end
    send_interval = 1.0 / math.max(1, cfg.ai_hz)
end

local function allocAIVid()
    local start = next_ai_vid
    repeat
        local vid = next_ai_vid
        next_ai_vid = next_ai_vid + 1
        if next_ai_vid > MAX_AI_VID then next_ai_vid = 200 end
        -- check not already in use
        local used = false
        for _, info in pairs(ai_vehicles) do
            if info.server_vid == vid then used = true; break end
        end
        if not used then return vid end
    until next_ai_vid == start
    return nil  -- all 55 slots full
end

local function randomModel()
    local pool = cfg.vehicle_pool
    return pool[math.random(1, #pool)]
end

-- ── TCP send ──────────────────────────────────────────────────────────────────
local function tcpSend(ptype, payload_str)
    if not socket then return end
    payload_str = payload_str or ""
    tcp_seq = tcp_seq + 1
    local header = string.pack("<BHIHb", ptype, player_id, tcp_seq, #payload_str, 0)
    local frame  = string.pack("<I", #header + #payload_str)
    socket:send(frame .. header .. payload_str)
end

local function udpSend(ptype, payload_bytes)
    if not udp_socket then return end
    local header = string.pack("<BHIHb", ptype, player_id, 0, #payload_bytes, 0)
    udp_socket:send(header .. payload_bytes)
end

-- ── AI vehicle management ─────────────────────────────────────────────────────
local function spawnAIVehicle()
    local model   = randomModel()
    local svid    = allocAIVid()
    if not svid then return end

    -- Spawn near origin; the AI will drive from there
    local gid = be:spawnVehicle(model, {x=0, y=0, z=0.5}, {0,0,0,1}, true)
    if not gid then
        log("E", "novaMP/bridge", "Failed to spawn AI vehicle " .. model)
        return
    end

    local veh = be:getVehicleByID(gid)
    if not veh then return end

    -- Enable BeamNG's native AI
    if cfg.ai_mode == "traffic" or cfg.ai_mode == "random" then
        veh:queueLuaCommand('ai.setMode("traffic")')
        veh:queueLuaCommand(
            string.format('ai.setSpeedMode("limit"); ai.setSpeed(%f)', cfg.ai_speed_limit))
    end
    -- parked: just leave stationary

    ai_vehicles[gid] = { server_vid = svid, spawn_time = os.clock() }
    log("D", "novaMP/bridge",
        ("Spawned AI vehicle model=%s gid=%d svid=%d"):format(model, gid, svid))
end

local function despawnAllAI()
    for gid, _ in pairs(ai_vehicles) do
        be:removeVehicleByID(gid)
    end
    ai_vehicles = {}
    next_ai_vid = 200
end

local function manageAICount()
    local active = 0
    for _ in pairs(ai_vehicles) do active = active + 1 end

    -- Prune removed vehicles
    for gid, _ in pairs(ai_vehicles) do
        if not be:getVehicleByID(gid) then
            ai_vehicles[gid] = nil
            active = active - 1
        end
    end

    -- Spawn up to target
    local target = cfg.ai_count
    while active < target do
        spawnAIVehicle()
        active = active + 1
    end

    -- Despawn excess
    if active > target then
        local to_remove = active - target
        for gid, _ in pairs(ai_vehicles) do
            if to_remove <= 0 then break end
            be:removeVehicleByID(gid)
            ai_vehicles[gid] = nil
            to_remove = to_remove - 1
        end
    end
end

-- ── State capture & send ──────────────────────────────────────────────────────
local function sendAIStates()
    for gid, info in pairs(ai_vehicles) do
        local veh = be:getVehicleByID(gid)
        if not veh then
            ai_vehicles[gid] = nil
        else
            local pos   = veh:getPosition()
            local rot   = veh:getRotation()
            local vel   = veh:getVelocity()
            local avel  = veh:getAngularVelocity() or {x=0,y=0,z=0}
            local rpm   = veh:getEngineRPM() or 0
            local gear  = veh:getGear() or 0
            local dmg   = veh:getDamage() or 0
            local elec  = veh:getElectrics()

            local health = math.floor((1.0 - math.min(dmg, 1.0)) * 65535)
            local lights = 0
            if elec then
                if elec.lights_state and elec.lights_state > 0 then lights = lights | 0x01 end
                if elec.brake        and elec.brake > 0.1       then lights = lights | 0x02 end
                if elec.reverse      and elec.reverse > 0.5     then lights = lights | 0x04 end
            end

            -- Build VehicleState payload (66 bytes)
            -- vehicle_id = server_vid (200-254), vflags = VF_IS_AI
            local payload = string.pack("<BBffffffffffffffff BBbfH",
                info.server_vid,
                VF_IS_AI,
                pos.x, pos.y, pos.z,
                rot.x, rot.y, rot.z, rot.w,
                vel.x, vel.y, vel.z,
                avel.x, avel.y, avel.z,
                0, 0, 0, 0,        -- wheels
                lights, gear, rpm, health)

            udpSend(PKT_VEHICLE_UPDATE, payload)
        end
    end
end

-- ── Packet handler ────────────────────────────────────────────────────────────
local function handlePacket(ptype, payload)
    if ptype == PKT_AUTH_RESPONSE then
        local ok, j = pcall(jsonDecode, payload)
        if ok and j.ok then
            player_id = j.player_id
            connected = true
            log("I", "novaMP/bridge", "Bridge authenticated! player_id=" .. player_id)
            manageAICount()
        else
            local err = (ok and j.error) or "unknown"
            log("E", "novaMP/bridge", "Auth failed: " .. err)
        end

    elseif ptype == PKT_SERVER_INFO then
        local ok, j = pcall(jsonDecode, payload)
        if ok then
            log("I", "novaMP/bridge", "Connected to server: " .. (j.name or "?"))
        end

    elseif ptype == PKT_KICK then
        log("W", "novaMP/bridge", "Bridge kicked: " .. payload)
        connected = false
    end
end

-- ── TCP poll ──────────────────────────────────────────────────────────────────
local function pollTCP()
    if not socket then return end
    local data = socket:receive()
    if not data or #data == 0 then return end
    recv_buf = recv_buf .. data

    while #recv_buf >= 4 do
        local frame_len = string.unpack("<I", recv_buf)
        if #recv_buf < 4 + frame_len then break end
        local packet = recv_buf:sub(5, 4 + frame_len)
        recv_buf     = recv_buf:sub(5 + frame_len)
        if #packet >= 10 then
            local ptype = string.byte(packet, 1)
            local plen  = string.unpack("<H", packet, 8)
            local pl    = packet:sub(11, 10 + plen)
            handlePacket(ptype, pl)
        end
    end
end

-- ── BeamNG Extension hooks ────────────────────────────────────────────────────
function M.onExtensionLoaded()
    math.randomseed(os.time())
    log("I", "novaMP/bridge", "NovaMP AI Bridge loading...")
    loadConfig()

    socket = be:createTcpSocket()
    if not socket:connect(cfg.host, cfg.port) then
        log("E", "novaMP/bridge", "TCP connect failed to " .. cfg.host .. ":" .. cfg.port)
        return
    end

    udp_socket = be:createUdpSocket()
    udp_socket:connect(cfg.host, cfg.port)

    -- Authenticate as the bridge (special username recognised by the server)
    local auth = jsonEncode({
        username        = "##ai_bridge##",
        token           = "",
        server_password = cfg.bridge_token,
        version         = "1.0.0",
    })
    tcpSend(PKT_AUTH_REQUEST, auth)
    log("I", "novaMP/bridge",
        "AUTH_REQUEST sent to " .. cfg.host .. ":" .. cfg.port)
end

function M.onUpdate(dt)
    if not socket then return end
    pollTCP()

    if not connected then return end

    send_timer = send_timer - dt
    if send_timer <= 0 then
        send_timer = send_interval
        manageAICount()
        sendAIStates()
    end
end

function M.onExtensionUnloaded()
    despawnAllAI()
    if socket     then socket:close()     end
    if udp_socket then udp_socket:close() end
    connected = false
    log("I", "novaMP/bridge", "AI Bridge unloaded.")
end

return M
