# Building NovaMP

---

## Prerequisites

| Tool / Library    | Minimum Version | Notes                                         |
|-------------------|-----------------|-----------------------------------------------|
| CMake             | 3.24            | Required for `CMakePresets.json` support      |
| Ninja             | 1.11            | Used by all presets; faster than `make`       |
| vcpkg             | latest          | Set `VCPKG_ROOT` environment variable         |
| **Windows** MSVC  | VS 2022 (19.34) | `cl.exe` with C++20 support                   |
| **Linux** GCC     | 12              | Or Clang 15+                                  |
| Git               | any             | Needed to bootstrap vcpkg                     |
| Docker (optional) | 24              | For containerised master server or game server|
| curl (optional)   | any             | Used by Docker healthcheck                    |

---

## 1 — Clone and Bootstrap

```bash
git clone https://github.com/BLK-TTAUDI/NovaMP.git
cd NovaMP

# Clone vcpkg if you don't already have it
git clone https://github.com/microsoft/vcpkg.git
# Bootstrap
./vcpkg/bootstrap-vcpkg.sh          # Linux / macOS
.\vcpkg\bootstrap-vcpkg.bat         # Windows PowerShell

# Point CMake at vcpkg (add to your shell profile for convenience)
export VCPKG_ROOT="$PWD/vcpkg"      # Linux / macOS
$env:VCPKG_ROOT = "$PWD\vcpkg"      # Windows PowerShell
```

---

## 2 — Build All Components (Quick Start)

```bash
# Configure (downloads and builds all vcpkg dependencies on first run)
cmake --preset release

# Build everything
cmake --build --preset release
```

Binaries are placed in:

| Binary                          | Location                                |
|---------------------------------|-----------------------------------------|
| Master server                   | `out/build/release/server/novaMP-master`|
| Dedicated game server           | `out/build/release/servers/novaMP-server`|
| Client launcher                 | `out/build/release/client/novaMP-client`|

`Resources/` directories are copied automatically by post-build steps.

---

## 3 — Build Individual Components

You can configure and build each sub-project independently:

### Master Server only

```bash
cd server
cmake -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Dedicated Game Server only

```bash
cd servers
cmake -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Client Launcher only

```bash
cd client
cmake -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

---

## 4 — Windows (MSVC)

Open a **Developer PowerShell for VS 2022** (or run `vcvarsall.bat amd64`),
then follow the steps above.  The CMake presets use the Ninja generator, which
works inside a VS developer shell without any extra flags.

```powershell
$env:VCPKG_ROOT = "C:\tools\vcpkg"   # adjust to your vcpkg location
cmake --preset release
cmake --build --preset release
```

The three binaries will have `.exe` extensions.

### Windows Defender / vcpkg note

vcpkg downloads and compiles a large number of libraries on first run; Windows
Defender may slow this down significantly.  Add your vcpkg folder to the
Defender exclusion list if build times are unacceptable.

---

## 5 — Linux (GCC or Clang)

```bash
# GCC (Ubuntu 22.04 example)
sudo apt install gcc-12 g++-12 ninja-build cmake git curl zip unzip tar pkg-config

export CC=gcc-12 CXX=g++-12
cmake --preset release
cmake --build --preset release
```

```bash
# Clang 15
sudo apt install clang-15 lld-15 ninja-build cmake git
export CC=clang-15 CXX=clang++-15
cmake --preset release
cmake --build --preset release
```

---

## 6 — Debug Build

```bash
cmake --preset debug
cmake --build --preset debug
```

Debug builds enable AddressSanitizer and UndefinedBehaviorSanitizer on Linux
GCC/Clang (configured in `CMakeLists.txt`).  Binaries land in
`out/build/debug/`.

---

## 7 — Docker

### Master Server

```bash
cd server
docker compose up --build -d
```

The container exposes port **8080**.  Data is persisted in the named volume
`master_data` (mapped to `/data` inside the container).  Edit
`server/config.toml` before building — or mount it as a bind volume.

Check health:

```bash
docker compose ps          # State should be "healthy"
curl http://localhost:8080/health
```

### Dedicated Game Server (manual Docker)

A `Dockerfile` is not included for the game server by default because server
operators typically run it on bare metal for latency reasons.  If you want to
containerise it:

```dockerfile
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y libstdc++6 libssl3
COPY out/build/release/servers/novaMP-server /usr/local/bin/
COPY servers/Resources /Resources
EXPOSE 4444/tcp 4444/udp 4445/tcp
CMD ["novaMP-server"]
```

---

## 8 — Running for the First Time

### Master Server

```bash
./out/build/release/server/novaMP-master
# Edit server/config.toml — set [auth] jwt_secret to a random string
```

### Dedicated Game Server

```bash
cp servers/ServerConfig.toml .    # copy to working dir
# Edit ServerConfig.toml — set server.name, master.url, ai_traffic, etc.
./out/build/release/servers/novaMP-server
```

### Client Launcher

```bash
./out/build/release/client/novaMP-client
# Prompts for username + password, then shows server browser
# Or direct connect:
./out/build/release/client/novaMP-client 192.168.1.10 4444
```

---

## 9 — CMake Presets Reference

`CMakePresets.json` defines two presets at the repo root:

| Preset    | Build type  | Generator | Output dir              |
|-----------|-------------|-----------|-------------------------|
| `debug`   | Debug       | Ninja     | `out/build/debug`       |
| `release` | Release     | Ninja     | `out/build/release`     |

Both presets set
`CMAKE_TOOLCHAIN_FILE=$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`
automatically, so you do not need to pass it on the command line when using a
preset.

---

## 10 — Testing

There is no automated test suite yet.  Manual smoke-test flow:

1. Start the master server: `./novaMP-master`
2. Start a game server pointing at it: edit `ServerConfig.toml` → `master.url`
3. Verify the server appears in the browser:
   `curl http://localhost:8080/servers`
4. Run the client launcher; pick the server.
5. BeamNG.drive should launch, load the map, and connect.
6. In the server console type `players` — your entry should appear.
7. Type `ai.status` — AI vehicles should be active.
8. Spawn a vehicle in-game; verify `VEHICLE_SPAWN` log line on the server.
9. Type `say Hello from the server` — message should appear in-game.
10. Press Enter in the client to disconnect; server should log the leave event.
