# NovaMP AI Traffic

NovaMP runs AI vehicles entirely on the dedicated server. Clients receive
the same `VEHICLE_UPDATE` (0x08) packets for AI as they do for real players —
no special client-side code is required.

---

## How It Works

1. **Road network** — on startup the server loads the road graph for the
   current map from `Resources/Server/maps/<mapname>/`.  If you exported
   waypoints from BeamNG (see [Road Network Export](#road-network-export))
   those are used.  Otherwise the server generates a procedural 400 × 400 m
   grid at 20 m spacing, which works on any flat map (e.g. `gridmap_v2`)
   without manual setup.

2. **A\* path planning** — each AI vehicle is given a destination node and
   finds the shortest path through the road graph.  When it arrives it picks
   a new destination automatically.

3. **Automatic speed regulation** — you do not need to set a speed.
   Each vehicle reads the posted speed limit from the road node it is
   currently targeting and drives at that limit multiplied by its own
   personality factor.  See [Speed Regulation](#speed-regulation) below.

4. **Player awareness** — the AI scans a 40 m forward arc every tick.
   When it detects a player or another AI vehicle it slows proportionally,
   shifts to the other side of the lane to go around, and — if still blocked
   after 8 seconds — replans to a different route.

5. **Indicators & intersections** — turn signals activate 28 m before any
   bend greater than ~20°.  At junctions with 3 or more exits the AI slows
   to 60 % speed and waits if another vehicle is already crossing.

6. **Spawn / despawn** — AI only exists near players.  A vehicle spawns
   within `spawn_dist` metres of a player when active count is below target,
   and despawns when further than `despawn_dist` from every connected player.

7. **Vehicle IDs 200–254** are reserved for AI.  Packets use
   `sender_id = 0xFFFF` and `vflags |= VF_IS_AI (0x01)`.

---

## Speed Regulation

Speed is **automatic** — you do not need to configure it.

Each AI vehicle has two speed influences:

| Influence | Description |
|-----------|-------------|
| **Road speed limit** | Read from the road node the vehicle is heading toward. Comes from the exported road data or defaults to 14 m/s (~50 km/h) on procedural grids. |
| **Personality factor** | A random multiplier assigned at spawn: **0.70 – 1.10 ×** the road limit. One vehicle might naturally cruise at 70 % of the limit, another at 110 %. |

On top of those two, speed is reduced dynamically each tick for:

- **Upcoming bends** — the AI looks 4 nodes ahead and measures the sharpest
  turn angle in the path.  Gentle curves (< 20°) have no effect.  Sharp bends
  (~70°) bring speed down to roughly 35 % of the cruise speed before the
  vehicle enters the corner, then it accelerates back out.
- **Obstacles** — players or AI in the forward arc reduce speed
  proportionally with distance (full brake at 8 m, gentle coast at 30 m).
- **Intersections** — 60 % speed on approach; full stop if the junction is
  occupied by another AI.

### `speed_limit` config option

The `speed_limit` value in `ServerConfig.toml` acts as a **hard ceiling**,
not the actual cruising speed.  Set it only if you want to cap the maximum
possible speed (e.g. on a server themed around city driving).

```toml
# No cap — vehicles drive at road speed limits naturally (recommended)
speed_limit = 0

# Hard cap at 50 km/h regardless of road data
speed_limit = 13.9
```

Setting `speed_limit = 0` (the default) means no cap is applied and the AI
uses only road data and personality to regulate speed.

---

## ServerConfig.toml Reference

```toml
[ai_traffic]
enabled        = true

# Maximum simultaneous AI vehicles (hard cap: 55, IDs 200-254)
count          = 10

# Hard speed ceiling in m/s.  0 = no cap (recommended — let road data decide).
speed_limit    = 0

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

| Mode      | Description |
|-----------|-------------|
| `traffic` | Follow the road graph; respect road speed limits and personality. |
| `random`  | Roam between random nodes; speed cap from `speed_limit` (or uncapped if 0). |
| `parked`  | Spawn at a node and stay stationary — decorative parked cars. |

---

## Road Network Export

For the best results, export the road network from BeamNG.drive using the
in-game tool included with the server:

1. Load the map in BeamNG.drive.
2. Open the Lua console (`~`) and run:
   ```lua
   exec("Resources/Server/tools/export_roads.lua")
   ```
3. Copy the generated `novaMP_roads.json` from your
   `levels/<mapname>/` folder to
   `Resources/Server/maps/<mapname>/novaMP_roads.json` on the server.

The exported file includes per-node speed limits and lane widths, which the
AI uses directly for speed regulation.  Without it the server falls back to
the procedural grid with a default 14 m/s limit.

---

## Console Commands

Commands can be entered in the server console (stdin) or via RCON.

| Command                    | Description |
|----------------------------|-------------|
| `ai.status`                | Print active count, mode, and speed cap. |
| `ai.spawn <n>`             | Immediately spawn `n` AI vehicles. |
| `ai.despawn_all`           | Remove all active AI vehicles. |
| `ai.set_count <n>`         | Set the target AI vehicle count (0 = disable). |
| `ai.set_speed_limit <mps>` | Set hard speed ceiling in m/s (0 = remove cap). |

### Examples

```
> ai.status
AI Traffic — active: 7 / target: 10 | mode: traffic | speed cap: none

> ai.set_count 20
[AI] Target count set to 20

> ai.set_speed_limit 0
[AI] Speed cap removed — AI will follow road speed limits

> ai.set_speed_limit 25
[AI] Speed cap set to 25.0 m/s

> ai.despawn_all
[AI] All AI vehicles despawned
```

---

## RCON

RCON listens on `rcon_port` (default **4445**) over TCP.

```
nc <server-ip> 4445
```

1. Server sends: `NovaMP RCON v1.0\n`
2. You send: `AUTH <password>\n`
3. Server responds `OK\n` or `DENIED\n`.
4. Send console commands one per line; server echoes the result.
5. Send `quit` to close the session.

---

## Lua Plugin Control

```lua
-- Scale AI count with player count; no speed config needed
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

-- Optionally cap speed for a specific event (e.g. slow city cruise night)
function onEventStart()
    NovaMP.AI.setSpeedLimit(8.3)  -- ~30 km/h cap
end

function onEventEnd()
    NovaMP.AI.setSpeedLimit(0)    -- back to automatic
end
```

See [lua_api.md](lua_api.md) for the full `NovaMP.AI.*` reference.

---

## Performance

All figures measured on an 8-core / 3.6 GHz server.

| Players | AI vehicles | CPU (ai thread) | Outbound UDP |
|---------|-------------|-----------------|--------------|
| 1       | 10          | < 1 %           | ~75 KB/s     |
| 16      | 20          | ~2 %            | ~445 KB/s    |
| 32      | 55 (max)    | ~4 %            | ~1.1 MB/s    |

AI vehicles broadcast at `update_rate_hz` (default 20 Hz), not at the player
sync rate, so bandwidth per AI vehicle is significantly lower than per player.

---

## Known Limitations

- AI vehicles do not physically collide with each other or with player
  vehicles — collision is handled locally inside BeamNG.drive.
- Procedural grid fallback is only suitable for flat maps.  Hilly or complex
  maps should use an exported road network for accurate speed limits and
  natural-looking paths.
- Maximum 55 simultaneous AI vehicles per server (IDs 200–254 inclusive).
