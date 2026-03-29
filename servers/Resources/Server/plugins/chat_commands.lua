-- servers/Resources/Server/plugins/chat_commands.lua
-- Built-in player chat commands

local commands = {}

local function register(name, help, fn)
    commands[name] = { help = help, fn = fn }
end

register("help", "Show this help", function(pid)
    local lines = {"Commands:"}
    for c, info in pairs(commands) do
        table.insert(lines, "  !" .. c .. " — " .. info.help)
    end
    MP.SendChatMessage(pid, table.concat(lines, "\n"))
end)

register("players", "List connected players", function(pid)
    local ps = MP.GetPlayers()
    if #ps == 0 then
        MP.SendChatMessage(pid, "No players online.")
        return
    end
    local names = {}
    for _, p in ipairs(ps) do table.insert(names, p.name) end
    MP.SendChatMessage(pid, "Players (" .. #ps .. "): " .. table.concat(names, ", "))
end)

register("ping", "Pong!", function(pid)
    MP.SendChatMessage(pid, "Pong!")
end)

register("rules", "Show server rules", function(pid)
    MP.SendChatMessage(pid,
        "Rules:\n1. Be respectful\n2. No cheating\n3. Have fun!")
end)

function onChatMessage(player_id, username, message)
    if message:sub(1, 1) ~= "!" then return nil end
    local parts = {}
    for w in message:gmatch("%S+") do table.insert(parts, w) end
    local cmd_name = parts[1]:sub(2)
    local cmd = commands[cmd_name]
    if cmd then
        cmd.fn(player_id, parts)
        return "blocked"
    end
    return nil
end
