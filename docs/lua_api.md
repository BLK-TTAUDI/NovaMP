# NovaMP Lua Plugin API

Plugins live in `Resources/Server/plugins/*.lua` and are loaded on server startup.

---

## Events

The server calls global functions in each plugin when events fire. All event
handlers are optional — only define the ones you need.

### `onInit()`

Called once after all plugins are loaded and the server is ready to accept
connections.

```lua
function onInit()
    print("My plugin initialised")
end
```

---

### `onPlayerAuth(player_id, player_name, ip_address) → string | nil`

Called during the auth handshake, **before** the player is added to the session.
Return a non-empty string to reject the connection (used as kick reason).
Return `nil` (or nothing) to allow.

```lua
function onPlayerAuth(player_id, player_name, ip)
    if player_name == "BadGuy" then
        return "You are banned."
    end
    -- allow
end
```

| Parameter    | Type   | Description                       |
|--------------|--------|-----------------------------------|
| player_id    | number | Tentative server-assigned ID      |
| player_name  | string | Username from AUTH_REQUEST        |
| ip_address   | string | Remote IP (no port)               |

**Return**: `string` (kick reason) or `nil` (allow)

---

### `onPlayerJoin(player_id, player_name)`

Called after the client sends `READY` and the player is fully in-session.

```lua
function onPlayerJoin(id, name)
    MP.SendChatMessage(-1, name .. " has joined the server!")
end
```

---

### `onPlayerDisconnect(player_id, player_name)`

Called when a player's TCP connection closes (clean or unclean).

```lua
function onPlayerDisconnect(id, name)
    MP.SendChatMessage(-1, name .. " has left the server.")
end
```

---

### `onChatMessage(player_id, player_name, message) → string | nil`

Called for every incoming chat message.

- Return a non-empty string to **replace** the broadcast message with that text.
- Return `nil` to broadcast as-is.
- Return `""` (empty string) to **suppress** the message entirely.

```lua
function onChatMessage(id, name, msg)
    if msg:sub(1,1) == "!" then
        -- handled elsewhere; suppress
        return ""
    end
    return "[" .. name .. "] " .. msg
end
```

---

### `onVehicleSpawn(player_id, vehicle_id, model)`

Called when a player spawns a vehicle.

```lua
function onVehicleSpawn(pid, vid, model)
    print(("Player %d spawned %s (vid=%d)"):format(pid, model, vid))
end
```

---

### `onVehicleDelete(player_id, vehicle_id)`

Called when a player deletes one of their vehicles.

---

### `onShutdown()`

Called just before the server process exits.

```lua
function onShutdown()
    -- flush custom logs, persist state, etc.
end
```

---

## MP API (`MP.*`)

The `MP` table provides core multiplayer server functions, matching the
BeamMP server Lua API for drop-in plugin compatibility.

---

### `MP.SendChatMessage(player_id, message)`

Send a chat message.

| Argument  | Type   | Description                                          |
|-----------|--------|------------------------------------------------------|
| player_id | number | Target player ID, or **-1** to broadcast to everyone |
| message   | string | Message text                                         |

```lua
MP.SendChatMessage(-1, "Server restarting in 5 minutes!")
MP.SendChatMessage(3,  "Welcome back, Admin!")
```

---

### `MP.KickPlayer(player_id, reason)`

Disconnect a player with a kick message.

```lua
MP.KickPlayer(7, "AFK too long")
```

---

### `MP.BanPlayer(player_id, reason)`

Disconnect and add the player's IP to the ban list.

```lua
MP.BanPlayer(2, "Cheating")
```

---

### `MP.GetPlayerName(player_id) → string`

Returns the username of the given player, or `""` if no such player exists.

```lua
local name = MP.GetPlayerName(3)   -- e.g. "Alice"
```

---

### `MP.GetPlayerCount() → number`

Returns the number of currently connected players.

```lua
local n = MP.GetPlayerCount()
MP.SendChatMessage(-1, "Players online: " .. n)
```

---

### `MP.GetPlayers() → table`

Returns an array of `{ id = number, name = string }` for every connected player.

```lua
local list = MP.GetPlayers()
for _, p in ipairs(list) do
    print(p.id, p.name)
end
```

---

### `MP.TriggerEvent(event_name, ...)`

Fire a named event, calling `event_name(...)` in every loaded plugin that
defines that global function.  Arguments are forwarded as-is.

```lua
-- fire a custom event
MP.TriggerEvent("onServerTick", os.clock())
```

---

## NovaMP AI API (`NovaMP.AI.*`)

Extensions beyond the BeamMP-compatible surface, specific to NovaMP's
server-side AI Traffic system.

---

### `NovaMP.AI.spawn(count)`

Immediately attempt to spawn `count` AI vehicles near current players,
up to the configured maximum.

```lua
NovaMP.AI.spawn(5)
```

---

### `NovaMP.AI.despawn(vehicle_id)`

Remove a specific AI vehicle by its server vehicle ID (200–254).

```lua
NovaMP.AI.despawn(200)
```

---

### `NovaMP.AI.despawnAll()`

Remove all currently active AI vehicles.

```lua
NovaMP.AI.despawnAll()
```

---

### `NovaMP.AI.setCount(n)`

Set the target number of simultaneous AI vehicles. The traffic loop will
spawn or despawn over the next few ticks to reach this count.

```lua
NovaMP.AI.setCount(10)
```

---

### `NovaMP.AI.setMode(mode)`

Change the AI driving mode.

| Mode      | Behaviour                                   |
|-----------|---------------------------------------------|
| `traffic` | Follow waypoint graph, obey speed limit     |
| `random`  | Pick random waypoints, ignore speed limit   |
| `parked`  | Spawn but remain stationary                 |

```lua
NovaMP.AI.setMode("traffic")
```

---

### `NovaMP.AI.setSpeedLimit(mps)`

Set the AI speed limit in **metres per second**.

```lua
NovaMP.AI.setSpeedLimit(13.9)  -- ~50 km/h
```

---

### `NovaMP.AI.status() → table`

Returns a snapshot of the current AI Traffic state.

```lua
local s = NovaMP.AI.status()
-- s.active   : number  — vehicles currently alive
-- s.target   : number  — configured target count
-- s.mode     : string  — current mode string
-- s.speed_limit : number — speed limit in m/s
```

---

## Full Example Plugin

```lua
-- Resources/Server/plugins/my_plugin.lua

local spawn_time = os.time()

function onInit()
    MP.SendChatMessage(-1, "MyPlugin loaded. AI starting up.")
    NovaMP.AI.setCount(8)
    NovaMP.AI.setMode("traffic")
    NovaMP.AI.setSpeedLimit(14)
end

function onPlayerAuth(id, name, ip)
    if name:len() < 3 then
        return "Username too short."
    end
end

function onPlayerJoin(id, name)
    local n = MP.GetPlayerCount()
    MP.SendChatMessage(-1, name .. " joined! (" .. n .. " online)")
    -- Scale AI with player count
    NovaMP.AI.setCount(math.min(20, n * 3))
end

function onPlayerDisconnect(id, name)
    local n = MP.GetPlayerCount()
    MP.SendChatMessage(-1, name .. " left. (" .. n .. " online)")
    NovaMP.AI.setCount(math.max(4, n * 3))
end

function onChatMessage(id, name, msg)
    if msg == "!uptime" then
        local secs = os.time() - spawn_time
        MP.SendChatMessage(id, ("Server up for %d minutes."):format(math.floor(secs/60)))
        return ""
    end
    if msg == "!ai" then
        local s = NovaMP.AI.status()
        MP.SendChatMessage(id, ("AI: %d/%d vehicles, mode=%s, speed=%.1f m/s")
            :format(s.active, s.target, s.mode, s.speed_limit))
        return ""
    end
end

function onShutdown()
    print("MyPlugin shutting down.")
end
```
