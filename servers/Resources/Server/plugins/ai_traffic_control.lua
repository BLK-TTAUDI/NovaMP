-- servers/Resources/Server/plugins/ai_traffic_control.lua
-- Full AI Traffic control plugin — admin chat commands + auto-scaling

function onInit()
    print("[AI Control] Loaded. Active AI vehicles: " .. NovaMP.AI.status())
end

-- Auto-scale AI count based on player count
function onPlayerJoin(player_id, username)
    local count = MP.GetPlayerCount()
    if count >= 4 then
        NovaMP.AI.setCount(10)
        NovaMP.AI.setSpeedLimit(10.0)
        print("[AI Control] High player count — AI reduced to 10 @ 10 m/s")
    else
        NovaMP.AI.setCount(20)
        NovaMP.AI.setSpeedLimit(14.0)
    end
end

function onPlayerDisconnect(player_id)
    if MP.GetPlayerCount() == 0 then
        NovaMP.AI.setCount(0)
        print("[AI Control] No players — AI hibernating")
    end
end

-- Admin chat commands for AI traffic
function onChatMessage(player_id, username, message)
    -- Only admins
    local is_admin = false
    for _, p in ipairs(MP.GetPlayers()) do
        if p.id == player_id and p.role == "admin" then
            is_admin = true; break
        end
    end
    if not is_admin then return nil end

    if message == "!ai spawn" then
        NovaMP.AI.spawn({ model = "etk800", x = 0, y = 0, z = 0 })
        MP.SendChatMessage(player_id, "AI vehicle spawned.")
        return "blocked"
    end

    if message == "!ai clear" then
        NovaMP.AI.despawnAll()
        MP.SendChatMessage(player_id, "All AI vehicles removed.")
        return "blocked"
    end

    if message == "!ai status" then
        MP.SendChatMessage(player_id, "AI active: " .. NovaMP.AI.status())
        return "blocked"
    end

    local n = message:match("^!ai count (%d+)$")
    if n then
        NovaMP.AI.setCount(tonumber(n))
        MP.SendChatMessage(player_id, "AI count -> " .. n)
        return "blocked"
    end

    local spd = message:match("^!ai speed ([%d%.]+)$")
    if spd then
        NovaMP.AI.setSpeedLimit(tonumber(spd))
        MP.SendChatMessage(player_id, "AI speed -> " .. spd .. " m/s")
        return "blocked"
    end

    local mode = message:match("^!ai mode (%a+)$")
    if mode then
        NovaMP.AI.setMode(mode)
        MP.SendChatMessage(player_id, "AI mode -> " .. mode)
        return "blocked"
    end

    return nil
end
