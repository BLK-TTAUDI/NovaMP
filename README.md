# NovaMP — Open-Source BeamNG.drive Multiplayer

NovaMP is a free, open-source multiplayer mod and server system for BeamNG.drive,
providing exact feature parity with BeamMP (as of 2026) plus **server-side authoritative
AI Traffic** — a first-class feature that runs entirely on the dedicated game server.

---

## Architecture Overview

```
  Players
    │
    │  (direct UDP/TCP connection)
    ▼
┌──────────────────────────┐        ┌──────────────────────────┐
│  Dedicated Game Server   │◄──────►│     Master Server        │
│  (servers/ — you host)   │ register│  (server/ — we host)     │
│                          │        │                          │
│  • 100 Hz vehicle sync   │        │  • Server browser API    │
│  • Lua plugin system     │        │  • User accounts / auth  │
│  • Server-side AI Traffic│        │  • Mod repository        │
│  • Chat, RCON, mods      │        │  • Ban list / stats      │
│  • Full gameplay logic   │        │  • NO gameplay logic     │
└──────────────────────────┘        └──────────────────────────┘
         ▲
         │  (BeamNG Lua mod injected by launcher)
         │
┌──────────────────────────┐
│    Client Launcher       │
│   (client/ — players)    │
│                          │
│  • Launches BeamNG.drive │
│  • Injects NovaMP mod    │
│  • Server browser UI     │
│  • Mod auto-download     │
└──────────────────────────┘
```

### server/ — Master Server (official backend, we operate this)
- Stores user accounts, hashed passwords, Discord OAuth tokens
- Server registration & heartbeat (game servers ping this)
- Public REST API for server browser
- Mod repository with version tracking
- Global ban list
- **Does NOT run any gameplay, physics, AI traffic, or Lua plugins**

### servers/ — Dedicated Game Server (what YOU download and host)
- Players connect directly via IP:port
- Full 100 Hz vehicle position/rotation/velocity synchronization
- Lua plugin system with BeamMP-compatible events
- Server-side AI Traffic: fully authoritative, server simulates and
  owns all AI vehicles, synced to all clients like player vehicles
- Admin console, RCON, configurable via `ServerConfig.toml`
- Docker-ready, cross-platform (Windows/Linux)

### client/ — Client Launcher
- Locates and launches BeamNG.drive with the NovaMP Lua mod
- Integrated server browser (talks to master server REST API)
- Automatic mod download/sync before joining a server
- Handles auth handshake with both master and game server

---

## Server-Side AI Traffic

Enable in `ServerConfig.toml`:

```toml
[ai_traffic]
enabled         = true
count           = 20          # number of AI vehicles
speed_limit     = 14.0        # m/s (~50 km/h)
mode            = "traffic"   # "traffic" | "random" | "parked"
update_rate_hz  = 20          # how often AI state is broadcast
despawn_dist    = 500.0       # meters from nearest player
spawn_dist      = 300.0       # meters from nearest player
vehicle_pool    = ["etk800", "vivace", "pessima"]
```

AI vehicles are broadcast using the same packet type as player vehicles
(`PKT_VEHICLE_UPDATE`) with the `is_ai` flag set. Clients render them
identically to human-driven cars.

Lua plugin control:

```lua
-- In a server plugin:
NovaMP.AI.spawn({ model = "etk800", pos = {x=0,y=0,z=0} })
NovaMP.AI.setCount(30)
NovaMP.AI.setMode("random")
NovaMP.AI.despawnAll()
```

Console / RCON commands:

```
ai.spawn                    — spawn one AI vehicle near a player
ai.despawn_all              — remove all AI vehicles
ai.set_count <n>            — change the AI count live
ai.set_speed_limit <mps>    — change speed limit live
ai.status                   — print AI traffic status
```

---

## Building

### Prerequisites
- CMake 3.20+
- vcpkg (set `VCPKG_ROOT`)
- C++20 compiler (MSVC 2022 / GCC 12 / Clang 15)
- (Optional) Docker + Docker Compose

### Quick build (all components)

```bash
git clone https://github.com/BLK-TTAUDI/NovaMP
cd novaMP
cmake --preset release
cmake --build build/release
```

### Windows (Visual Studio 2022)

```bat
git clone https://github.com/BLK-TTAUDI/NovaMP
cd novaMP
set VCPKG_ROOT=C:\vcpkg
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
```

### Linux (GCC/Clang)

```bash
export VCPKG_ROOT=$HOME/vcpkg
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Docker (master server)

```bash
cd server
docker compose up -d
```

### Docker (dedicated game server)

```bash
cd servers
docker compose up -d
```

---

## Quick Start — Hosting a Server

1. Download the latest release from GitHub Releases.
2. Extract to a folder.
3. Edit `ServerConfig.toml` (set name, max players, auth token, etc.).
4. Run `start.bat` (Windows) or `./start.sh` (Linux).
5. Optionally register with the master server for public listing:
   set `[master] register = true` and `auth_token` in the config.

---

## License

Copyright (c) 2026 NovaMP / BLK-TTAUDI. All rights reserved.

Source code is made available for inspection only. You may not copy, modify,
or redistribute it. See [LICENSE](LICENSE) for full terms.

NovaMP is not affiliated with or endorsed by BeamNG GmbH or BeamMP.
BeamNG.drive is a trademark of BeamNG GmbH.
