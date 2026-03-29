-- client/Resources/Client/vehicle_sync.lua
-- Sends local vehicle state to server (30 Hz) and applies remote vehicle states.

local M = {}

local PKT_VEHICLE_UPDATE = 0x08
local VF_IS_AI           = 0x01

local local_vehicles  = {}  -- vid -> tracking data
local remote_vehicles = {}  -- player_id -> { vehicle_id -> game_vid }
local ai_vehicles     = {}  -- server_vid -> game_vid

local SEND_INTERVAL = 1.0 / 30  -- 30 Hz
local send_timer    = 0.0

-- ── Send local player vehicle state ──────────────────────────────────────────
function M.onUpdate(dt, udp_send_fn)
    send_timer = send_timer - dt
    if send_timer > 0 then return end
    send_timer = SEND_INTERVAL

    local veh = be:getPlayerVehicle(0)
    if not veh then return end

    local vid   = veh:getID()
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
        if elec.brake         and elec.brake > 0.1      then lights = lights | 0x02 end
        if elec.reverse       and elec.reverse > 0.5    then lights = lights | 0x04 end
        if elec.signal_left_input                        then lights = lights | 0x08 end
        if elec.signal_right_input                       then lights = lights | 0x10 end
    end

    -- VehicleState struct (66 bytes):
    -- vehicle_id(1B) vflags(1B) pos(12B) rot(16B) vel(12B) angvel(12B)
    -- wheels(4B) lights(1B) gear(1B) rpm(4B) health(2B)
    local payload = string.pack("<BBffffffffffffffff BBbfH",
        vid, 0,
        pos.x, pos.y, pos.z,
        rot.x, rot.y, rot.z, rot.w,
        vel.x, vel.y, vel.z,
        avel.x, avel.y, avel.z,
        0, 0, 0, 0,
        lights, gear, rpm, health)

    udp_send_fn(PKT_VEHICLE_UPDATE, payload)
end

-- ── Remote vehicle spawn (player or AI) ──────────────────────────────────────
function M.onRemoteVehicleSpawn(player_id, vehicle_id, model, config_json)
    local gid = be:spawnVehicle(model, {x=0,y=0,z=0}, {0,0,0,1}, true)
    if not gid then
        log("E", "novaMP/sync", "Failed to spawn " .. model)
        return
    end
    if not remote_vehicles[player_id] then remote_vehicles[player_id] = {} end
    remote_vehicles[player_id][vehicle_id] = gid
    log("D", "novaMP/sync",
        "Remote spawn: player=" .. player_id .. " vid=" .. vehicle_id ..
        " model=" .. model .. " gid=" .. gid)
end

-- ── Apply incoming vehicle state (UDP) ───────────────────────────────────────
function M.onRemoteVehicleUpdate(payload)
    if #payload < 66 then return end

    local vid, vflags,
          px, py, pz,
          rx, ry, rz, rw,
          vx, vy, vz,
          ax, ay, az,
          _w0, _w1, _w2, _w3,
          lights, gear, rpm, health_u16 =
        string.unpack("<BBffffffffffffffff BBbfH", payload)

    local is_ai  = (vflags & VF_IS_AI) ~= 0
    local game_vid = nil

    if is_ai then
        game_vid = ai_vehicles[vid]
        if not game_vid then
            -- Lazy-spawn the AI vehicle on first update
            game_vid = be:spawnVehicle("etk800", {x=px,y=py,z=pz}, {rx,ry,rz,rw}, true)
            ai_vehicles[vid] = game_vid
        end
    else
        for _, vids in pairs(remote_vehicles) do
            if vids[vid] then game_vid = vids[vid]; break end
        end
    end

    if not game_vid then return end
    local veh = be:getVehicleByID(game_vid)
    if not veh then return end

    veh:setPositionRotation(px, py, pz, rx, ry, rz, rw)
    veh:setVelocity(vx, vy, vz)
end

-- ── Cleanup ───────────────────────────────────────────────────────────────────
function M.onRemoteVehicleDelete(vid)
    for _, vids in pairs(remote_vehicles) do
        if vids[vid] then be:removeVehicleByID(vids[vid]); vids[vid] = nil end
    end
    if ai_vehicles[vid] then
        be:removeVehicleByID(ai_vehicles[vid]); ai_vehicles[vid] = nil
    end
end

function M.onPlayerLeave(player_id)
    local vids = remote_vehicles[player_id]
    if vids then
        for _, gid in pairs(vids) do be:removeVehicleByID(gid) end
        remote_vehicles[player_id] = nil
    end
end

function M.onLocalVehicleSpawned(vid)  local_vehicles[vid] = true end
function M.onLocalVehicleDestroyed(vid) local_vehicles[vid] = nil end

return M
