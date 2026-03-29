-- client/Resources/Client/main.lua
-- NovaMP BeamNG.drive Client Mod — Extension entry point
-- Place the novaMP folder (or zip) in Documents/BeamNG.drive/mods/

local M = {}

local vehicle_sync = require("extensions/novaMP/vehicle_sync")
local ai_authority = require("extensions/novaMP/ai_authority")

-- ── State ─────────────────────────────────────────────────────────────────────
local connected    = false
local player_id    = 0
local socket       = nil
local udp_socket   = nil
local recv_buf     = ""
local tcp_seq      = 0

-- Packet type constants for the authority negotiation
local PKT_AUTHORITY_CLAIM  = 0x20
local PKT_AUTHORITY_GRANT  = 0x21
local PKT_AUTHORITY_REVOKE = 0x22

-- ── Read connect config written by the launcher ───────────────────────────────
local function loadConnectConfig()
    local path = "/userdata/novaMP_connect.json"
    local f = io.open(path, "r")
    if not f then return nil end
    local data = f:read("*a"); f:close()
    local ok, cfg = pcall(jsonDecode, data)
    return ok and cfg or nil
end

-- ── Framed TCP send ───────────────────────────────────────────────────────────
local function tcpSend(ptype, payload_str)
    if not socket then return end
    payload_str = payload_str or ""
    -- Header: type(1) sender(2) seq(4) plen(2) flags(1) = 10 bytes
    tcp_seq = tcp_seq + 1
    local header  = string.pack("<BHIHb", ptype, player_id, tcp_seq, #payload_str, 0)
    local frame   = string.pack("<I", #header + #payload_str)
    socket:send(frame .. header .. payload_str)
end

local function udpSend(ptype, payload_bytes)
    if not udp_socket then return end
    local header = string.pack("<BHIHb", ptype, player_id, 0, #payload_bytes, 0)
    udp_socket:send(header .. payload_bytes)
end

-- ── Connect ───────────────────────────────────────────────────────────────────
local function connect(host, port, username, token, server_password)
    log("I", "novaMP", "Connecting to " .. host .. ":" .. tostring(port))
    socket = be:createTcpSocket()
    if not socket:connect(host, port) then
        log("E", "novaMP", "TCP connect failed")
        return false
    end
    udp_socket = be:createUdpSocket()
    udp_socket:connect(host, port)

    local auth = jsonEncode({
        username        = username,
        token           = token or "",
        server_password = server_password or "",
        version         = "1.0.0"
    })
    tcpSend(0x01, auth)  -- AUTH_REQUEST
    return true
end

-- ── Incoming packet handler ───────────────────────────────────────────────────
local function handlePacket(ptype, payload)
    -- AUTH_RESPONSE
    if ptype == 0x02 then
        local ok, j = pcall(jsonDecode, payload)
        if not ok then return end
        if j.ok then
            player_id = j.player_id
            connected = true
            log("I", "novaMP", "Authenticated! Player ID=" .. player_id)
            -- Tell server we are ready once map loads
            -- tcpSend(0x17)  -- READY (called in onVehicleReady)
        else
            log("E", "novaMP", "Auth failed: " .. (j.error or "unknown"))
        end

    -- SERVER_INFO
    elseif ptype == 0x0C then
        local ok, j = pcall(jsonDecode, payload)
        if ok then
            log("I", "novaMP", "Server: " .. j.name ..
                " | Map: " .. j.map ..
                " | AI: " .. tostring(j.ai_enabled))
        end

    -- PLAYER_JOIN
    elseif ptype == 0x03 then
        local ok, j = pcall(jsonDecode, payload)
        if ok then log("I", "novaMP", j.name .. " joined.") end

    -- PLAYER_LEAVE
    elseif ptype == 0x04 then
        local ok, j = pcall(jsonDecode, payload)
        if ok then
            vehicle_sync.onPlayerLeave(j.id)
            log("I", "novaMP", "Player " .. j.id .. " left.")
        end

    -- VEHICLE_SPAWN
    elseif ptype == 0x06 then
        local ok, j = pcall(jsonDecode, payload)
        if ok then
            vehicle_sync.onRemoteVehicleSpawn(j.player_id, j.vehicle_id, j.model, j.config)
        end

    -- VEHICLE_DELETE
    elseif ptype == 0x07 then
        if #payload >= 1 then
            vehicle_sync.onRemoteVehicleDelete(string.byte(payload, 1))
        end

    -- VEHICLE_UPDATE  (UDP)
    elseif ptype == 0x08 then
        vehicle_sync.onRemoteVehicleUpdate(payload)

    -- CHAT_MESSAGE
    elseif ptype == 0x0B then
        guiDisplayMessage(payload, 5, "normal")
        log("I", "novaMP/chat", payload)

    -- KICK
    elseif ptype == 0x18 then
        connected = false
        ai_authority.deactivate()
        guiDisplayMessage("Kicked: " .. payload, 10, "error")
        log("W", "novaMP", "Kicked: " .. payload)

    -- AUTHORITY_GRANT — server wants us to drive BeamNG AI traffic
    elseif ptype == PKT_AUTHORITY_GRANT then
        local ok, j = pcall(jsonDecode, payload)
        if ok then
            ai_authority.activate(
                udpSend,
                j.update_hz   or 20,
                j.ai_count    or 10,
                j.speed_limit or 14.0,
                j.mode        or "traffic")
            guiDisplayMessage("[NovaMP] You are now the AI traffic authority.", 5, "normal")
        end

    -- AUTHORITY_REVOKE — server is taking authority away
    elseif ptype == PKT_AUTHORITY_REVOKE then
        ai_authority.deactivate()
        guiDisplayMessage("[NovaMP] AI authority transferred.", 5, "normal")
    end
end

-- ── TCP poll (called from onUpdate) ──────────────────────────────────────────
local function pollTCP()
    if not socket then return end
    local data = socket:receive()
    if not data or #data == 0 then return end
    recv_buf = recv_buf .. data

    while #recv_buf >= 4 do
        local frame_len = string.unpack("<I", recv_buf)
        if #recv_buf < 4 + frame_len then break end
        local packet = recv_buf:sub(5, 4 + frame_len)
        recv_buf = recv_buf:sub(5 + frame_len)
        if #packet >= 10 then
            local ptype = string.byte(packet, 1)
            local plen  = string.unpack("<H", packet, 8)
            local pl    = packet:sub(11, 10 + plen)
            handlePacket(ptype, pl)
        end
    end
end

-- ── UDP poll ──────────────────────────────────────────────────────────────────
local function pollUDP()
    if not udp_socket then return end
    local data = udp_socket:receive()
    if not data or #data < 10 then return end
    local ptype = string.byte(data, 1)
    local plen  = string.unpack("<H", data, 8)
    local pl    = data:sub(11, 10 + plen)
    handlePacket(ptype, pl)
end

-- ── BeamNG Extension hooks ────────────────────────────────────────────────────
function M.onExtensionLoaded()
    log("I", "novaMP", "NovaMP client mod v1.0.0 loaded.")
    local cfg = loadConnectConfig()
    if cfg and cfg.host and cfg.port then
        connect(cfg.host, cfg.port,
                cfg.username or "Player",
                cfg.token    or "", "")
    else
        log("W", "novaMP", "No connect config. Use the NovaMP launcher.")
    end
end

function M.onUpdate(dt)
    if not socket then return end
    pollTCP()
    pollUDP()
    if connected then
        vehicle_sync.onUpdate(dt, udpSend)
        -- If we are the AI authority, capture and forward AI vehicle states
        if ai_authority.isActive() then
            ai_authority.onUpdate(dt)
        end
    end
end

function M.onVehicleSpawned(vid)
    if not connected then return end
    local veh = be:getVehicleByID(vid)
    if not veh then return end
    tcpSend(0x06, jsonEncode({ model = veh:getJBeamFilename() or "unknown", config = "{}" }))
    vehicle_sync.onLocalVehicleSpawned(vid)
    ai_authority.addPlayerVehicle(vid)
end

function M.onVehicleDestroyed(vid)
    if not connected then return end
    tcpSend(0x07, string.char(vid))
    vehicle_sync.onLocalVehicleDestroyed(vid)
    ai_authority.removePlayerVehicle(vid)
end

function M.sendChatMessage(msg)
    if connected then tcpSend(0x0B, msg) end
end

function M.onExtensionUnloaded()
    if socket then socket:close() end
    if udp_socket then udp_socket:close() end
    connected = false
    log("I", "novaMP", "NovaMP client mod unloaded.")
end

return M
