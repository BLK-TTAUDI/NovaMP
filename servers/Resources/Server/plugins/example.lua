-- servers/Resources/Server/plugins/example.lua
-- Example NovaMP server plugin — demonstrates the full BeamMP-compatible event API

function onInit()
    print("[Example] Plugin loaded. Players: " .. MP.GetPlayerCount())
end

-- Return a non-empty string to reject the connecting player.
function onPlayerAuth(player_id, username)
    print("[Example] Auth: " .. username .. " (id=" .. player_id .. ")")
    -- Uncomment to block a specific user:
    -- if username == "banned_user" then return "You are not welcome here." end
    return nil
end

function onPlayerJoin(player_id, username)
    print("[Example] Join: " .. username)
    MP.SendChatMessage(player_id, "Welcome, " .. username .. "!")
    MP.SendChatMessage(-1, username .. " joined the server.")
end

function onPlayerDisconnect(player_id)
    local name = MP.GetPlayerName(player_id)
    print("[Example] Leave: " .. name)
    MP.SendChatMessage(-1, name .. " left the server.")
end

-- Return a non-empty string to block the message from being broadcast.
function onChatMessage(player_id, username, message)
    print("[Chat] " .. username .. ": " .. message)

    if message == "!players" then
        local players = MP.GetPlayers()
        local names = {}
        for _, p in ipairs(players) do table.insert(names, p.name) end
        MP.SendChatMessage(player_id, "Online (" .. #players .. "): " .. table.concat(names, ", "))
        return "blocked"
    end

    return nil
end

function onVehicleSpawn(player_id, model)
    print("[Example] Vehicle spawn: player=" .. player_id .. " model=" .. model)
end

function onVehicleDelete(player_id, vehicle_id)
    print("[Example] Vehicle delete: player=" .. player_id .. " vid=" .. vehicle_id)
end

function onShutdown()
    print("[Example] Server shutting down.")
end
