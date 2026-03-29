# NovaMP AI Traffic

NovaMP runs AI vehicles entirely on the dedicated game server.  Clients receive
the same `VEHICLE_UPDATE` (0x08) packets for AI as they do for other players;
no special client-side code is required.

---

## How It Works

1. **Waypoint graph** — on startup the server loads
   `Resources/Server/waypoints.json` (if present).  Each waypoint has a
   position and a list of neighbours.  The traffic loop picks a random
   destination for each AI vehicle and follows the shortest path.

2. **Procedural fallback** — if no `waypoints.json` exists the server
   generates a flat 400 × 400 m grid at 20 m spacing centred on the origin.
   This works on any flat map (e.g. `gridmap_v2`) without manual setup.

3. **Tick loop** — AI runs at `update_rate_hz` (default 20 Hz) independently
   of the 100 Hz player vehicle sync loop.

4. **Spawn / despawn** — AI vehicles only exist near players.  A vehicle
   spawns within `spawn_dist` metres of a player when the active count is below
   the target, and despawns when it is more than `despawn_dist` metres from
   every connected player.

5. **Vehicle IDs 200–254** are reserved exclusively for AI.  AI packets use
   `sender_id = 0xFFFF` and `vflags |= VF_IS_AI (0x01)`.

---

## ServerConfig.toml Reference

```toml
[ai_traffic]
enabled        = true

# Maximum simultaneous AI vehicles (hard cap: 55, IDs 200-254)
count          = 10

# Speed limit in m/s  (13.9 ≈ 50 km/h)
speed_limit    = 13.9

# "traffic" | "random" | "parked"
mode           = "traffic"

# Tick rate for AI simulation (independent of vehicle sync)
update_rate_hz = 20

# Despawn AI that is farther than this from every player (metres)
despawn_dist   = 300.0

# Spawn new AI within this radius of any player (metres)
spawn_dist     = 150.0

# Path re-plan interval (seconds) — how often each AI picks a new goal
path_replan_secs = 30.0

# Vehicle models to randomly pick from when spawning AI
vehicle_pool   = ["etk800", "etki", "vivace", "covet", "miramar"]
```

### Mode descriptions

| Mode      | Description                                                      |
|-----------|------------------------------------------------------------------|
| `traffic` | Follow the waypoint graph; respect `speed_limit`                 |
| `random`  | Teleport between random waypoints; ignore speed limit            |
| `parked`  | Spawn at a waypoint and remain stationary (decorative traffic)   |

---

## waypoints.json Format

Place this file at `Resources/Server/waypoints.json`.

```json
{
  "waypoints": [
    { "id": 0, "x":   0.0, "y":   0.0, "z": 0.0, "neighbors": [1, 4] },
    { "id": 1, "x":  20.0, "y":   0.0, "z": 0.0, "neighbors": [0, 2] },
    { "id": 2, "x":  20.0, "y":  20.0, "z": 0.0, "neighbors": [1, 3] },
    { "id": 3, "x":   0.0, "y":  20.0, "z": 0.0, "neighbors": [2, 0] },
    { "id": 4, "x": -20.0, "y":   0.0, "z": 0.0, "neighbors": [0]    }
  ]
}
```

**Rules:**
- IDs must be unique non-negative integers.
- `neighbors` lists the IDs of directly reachable waypoints (one-way edges are
  fine; the path planner follows directed edges).
- Coordinates are world-space metres matching BeamNG.drive's coordinate system.
- Minimum recommended spacing: 5 m (closer may cause jitter at high speed).
- The waypoint graph does not need to be fully connected; isolated sub-graphs
  are fine — AI vehicles will stay within their connected component.

---

## Console Commands

Commands can be entered in the server's interactive console (stdin) or sent
via RCON.

| Command                    | Description                                          |
|----------------------------|------------------------------------------------------|
| `ai.status`                | Print current AI state (count, mode, speed limit)    |
| `ai.spawn <n>`             | Immediately spawn `n` AI vehicles                    |
| `ai.despawn_all`           | Remove all active AI vehicles                        |
| `ai.set_count <n>`         | Set the target AI vehicle count (0 to disable)       |
| `ai.set_speed_limit <mps>` | Set speed limit in metres per second                 |

### Examples

```
> ai.status
AI Traffic — active: 7 / target: 10 | mode: traffic | speed: 13.9 m/s

> ai.set_count 20
[AI] Target count set to 20

> ai.set_speed_limit 25
[AI] Speed limit set to 25.0 m/s

> ai.despawn_all
[AI] All AI vehicles despawned
```

---

## RCON

RCON listens on `rcon_port` (default **4445**) over TCP.  Connect with any
raw TCP client (e.g. netcat, PuTTY raw mode):

```
nc <server-ip> 4445
```

1. The server sends: `NovaMP RCON v1.0\n`
2. You send: `AUTH <password>\n`
3. Server responds `OK\n` on success or `DENIED\n` on failure.
4. Send any console command (one per line); the server echoes the result.
5. Send `quit` to close the RCON session (does not stop the server).

### RCON session example

```
NovaMP RCON v1.0
AUTH myrconpassword
OK
players
Connected players (3): Alice(1) Bob(2) Carol(3)
ai.status
AI Traffic — active: 10 / target: 10 | mode: traffic | speed: 13.9 m/s
say Server maintenance in 10 minutes!
[Console] Server maintenance in 10 minutes!
quit
```

---

## Lua Plugin Control

Plugins can read and adjust AI Traffic at runtime via `NovaMP.AI.*`:

```lua
-- Scale AI with player count
function onPlayerJoin(id, name)
    local n = MP.GetPlayerCount()
    NovaMP.AI.setCount(math.min(20, n * 4))
end

function onPlayerDisconnect(id, name)
    local n = MP.GetPlayerCount()
    if n == 0 then
        NovaMP.AI.despawnAll()
    else
        NovaMP.AI.setCount(math.max(5, n * 4))
    end
end
```

See [lua_api.md](lua_api.md) for the full `NovaMP.AI.*` reference.

---

## Performance

All figures measured on an 8-core / 3.6 GHz server.

| Players | AI vehicles | CPU (ai thread) | Outbound UDP     |
|---------|-------------|-----------------|------------------|
| 1       | 10          | < 1 %           | ~75 KB/s         |
| 16      | 20          | ~2 %            | ~445 KB/s        |
| 32      | 55 (max)    | ~4 %            | ~1.1 MB/s        |

AI vehicles broadcast at `update_rate_hz` (default 20 Hz), not 100 Hz, so
bandwidth cost per AI vehicle is ~5× lower than per player vehicle.

---

## Known Limitations

- AI vehicles do not collide with each other or with player vehicles
  (collision is handled locally in BeamNG.drive, not server-side).
- Path planning is a simple greedy nearest-waypoint advance — no A\* or
  traffic-light awareness.
- The procedural grid fallback only suits flat maps; hilly maps should provide
  a hand-crafted `waypoints.json`.
- Maximum 55 AI vehicles per server (IDs 200–254 inclusive).
