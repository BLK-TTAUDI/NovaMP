-- client/Resources/Client/ai_authority.lua
--
-- NovaMP AI Authority — loaded by main.lua when the server grants this client
-- AUTHORITY_GRANT (0x21).
--
-- When active this module:
--   1. Enables BeamNG's AI traffic via aiManager.
--   2. At send_hz, iterates every vehicle on the map and sends states for
--      vehicles that are NOT owned by any human player (i.e. AI traffic).
--   3. Uses server vehicle IDs 200-254 mapped from BeamNG's local game IDs.
--   4. Sends VEHICLE_UPDATE (0x08) with VF_IS_AI = 0x01.
--
-- When the server sends AUTHORITY_REVOKE (0x22) main.lua calls M.deactivate().

local M = {}

local VF_IS_AI    = 0x01
local PKT_UPDATE  = 0x08

local active       = false
local send_hz      = 20
local send_interval= 1.0 / send_hz
local send_timer   = 0.0

-- Maps BeamNG game_vid → server_vid (200-254)
local vid_map      = {}
local next_svid    = 200
local MAX_SVID     = 254

-- Set by main.lua so we share the same udpSend function
local udp_send_fn  = nil

-- Player-owned vehicle IDs that we must never send as AI
local player_vids  = {}

-- ── Internal helpers ──────────────────────────────────────────────────────────
local function allocSVID(game_vid)
    if vid_map[game_vid] then return vid_map[game_vid] end
    -- find a free slot
    local used = {}
    for _, sv in pairs(vid_map) do used[sv] = true end
    for sv = 200, MAX_SVID do
        if not used[sv] then
            vid_map[game_vid] = sv
            return sv
        end
    end
    return nil  -- 55-vehicle cap reached
end

local function buildVehicleStatePayload(game_vid, svid)
    local veh = be:getVehicleByID(game_vid)
    if not veh then return nil end

    local pos  = veh:getPosition()
    local rot  = veh:getRotation()
    local vel  = veh:getVelocity()
    local avel = veh:getAngularVelocity() or {x=0,y=0,z=0}
    local rpm  = veh:getEngineRPM()  or 0
    local gear = veh:getGear()       or 0
    local dmg  = veh:getDamage()     or 0
    local elec = veh:getElectrics()

    local health = math.floor((1.0 - math.min(dmg, 1.0)) * 65535)
    local lights = 0
    if elec then
        if elec.lights_state and elec.lights_state > 0 then lights = lights | 0x01 end
        if elec.brake        and elec.brake > 0.1       then lights = lights | 0x02 end
        if elec.reverse      and elec.reverse > 0.5     then lights = lights | 0x04 end
        if elec.signal_left_input                        then lights = lights | 0x08 end
        if elec.signal_right_input                       then lights = lights | 0x10 end
    end

    return string.pack("<BBffffffffffffffff BBbfH",
        svid,
        VF_IS_AI,
        pos.x,  pos.y,  pos.z,
        rot.x,  rot.y,  rot.z, rot.w,
        vel.x,  vel.y,  vel.z,
        avel.x, avel.y, avel.z,
        0, 0, 0, 0,    -- wheels (future use)
        lights, gear, rpm, health)
end

-- ── Public API ────────────────────────────────────────────────────────────────

-- Called by main.lua when AUTHORITY_GRANT arrives.
function M.activate(send_fn, target_hz, target_count, speed_limit, mode)
    if active then return end
    active       = true
    udp_send_fn  = send_fn
    send_hz      = target_hz or 20
    send_interval= 1.0 / math.max(1, send_hz)
    send_timer   = 0.0
    vid_map      = {}
    next_svid    = 200

    -- Enable BeamNG's built-in AI traffic
    if aiManager then
        aiManager.setActive(true)
        if target_count then aiManager.setNumberOfVehicles(target_count) end
        if speed_limit   then aiManager.setSpeedLimit(speed_limit) end
        if mode == "parked" then
            aiManager.setMode("disabled")
        else
            aiManager.setMode(mode or "traffic")
        end
    else
        -- Older BeamNG versions: use the traffic Lua module
        local traffic = extensions.load("traffic")
        if traffic then
            traffic.activate(target_count or 10)
        end
    end

    log("I", "novaMP/authority", "AI Authority activated — driving BeamNG traffic AI.")
end

-- Called by main.lua when AUTHORITY_REVOKE arrives or we disconnect.
function M.deactivate()
    if not active then return end
    active = false

    if aiManager then
        aiManager.setActive(false)
    else
        local traffic = extensions.get("traffic")
        if traffic then traffic.deactivate() end
    end

    vid_map     = {}
    udp_send_fn = nil
    log("I", "novaMP/authority", "AI Authority deactivated.")
end

-- Called by main.lua every frame when we are the authority.
function M.onUpdate(dt)
    if not active or not udp_send_fn then return end

    send_timer = send_timer - dt
    if send_timer > 0 then return end
    send_timer = send_interval

    -- Iterate all vehicles in the scene
    local count = be:getVehicleCount()
    for i = 0, count - 1 do
        local veh = be:getVehicle(i)
        if veh then
            local gid = veh:getID()
            -- Skip player-owned vehicles
            if not player_vids[gid] then
                local svid = allocSVID(gid)
                if svid then
                    local payload = buildVehicleStatePayload(gid, svid)
                    if payload then
                        udp_send_fn(PKT_UPDATE, payload)
                    end
                end
            end
        end
    end

    -- Clean up stale mappings for vehicles that no longer exist
    for gid, _ in pairs(vid_map) do
        if not be:getVehicleByID(gid) then
            vid_map[gid] = nil
        end
    end
end

-- Called by main.lua when a local player vehicle is spawned/destroyed
-- so we never misidentify it as AI.
function M.addPlayerVehicle(vid)    player_vids[vid] = true  end
function M.removePlayerVehicle(vid) player_vids[vid] = nil   end

function M.isActive() return active end

return M
