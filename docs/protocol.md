# NovaMP Network Protocol

## Overview

NovaMP uses two transport channels per client:

| Channel | Protocol | Port  | Purpose                              |
|---------|----------|-------|--------------------------------------|
| Control | TCP      | 4444  | Auth, chat, spawn events, mods       |
| State   | UDP      | 4444  | Vehicle updates (high frequency)     |
| RCON    | TCP      | 4445  | Admin remote control                 |

Both game channels share the same port; the OS disambiguates TCP and UDP.

---

## Packet Header (10 bytes, all packets)

```
Offset  Size  Type    Field
0       1     uint8   packet_type   (PacketType enum)
1       2     uint16  sender_id     (0 = server, 0xFFFF = broadcast/AI)
3       4     uint32  sequence      (monotonic per sender per channel)
7       2     uint16  payload_len   (bytes after this 10-byte header)
9       1     uint8   flags         (PacketFlags bitmask)
```

All multibyte integers are **little-endian**.

**TCP framing**: Each TCP packet is preceded by a 4-byte LE `uint32`
containing the total length of `(header + payload)`. The receiver buffers
bytes until the complete frame is available before dispatching.

**UDP**: One packet per datagram; no framing prefix needed.

---

## PacketType Values

| Hex   | Name              | Channel | Direction        |
|-------|-------------------|---------|------------------|
| 0x01  | AUTH_REQUEST      | TCP     | Client → Server  |
| 0x02  | AUTH_RESPONSE     | TCP     | Server → Client  |
| 0x03  | PLAYER_JOIN       | TCP     | Server → All     |
| 0x04  | PLAYER_LEAVE      | TCP     | Server → All     |
| 0x05  | PLAYER_INFO       | TCP     | Server → Client  |
| 0x06  | VEHICLE_SPAWN     | TCP     | Both             |
| 0x07  | VEHICLE_DELETE    | TCP     | Both             |
| 0x08  | VEHICLE_UPDATE    | UDP     | Both             |
| 0x0B  | CHAT_MESSAGE      | TCP     | Both             |
| 0x0C  | SERVER_INFO       | TCP     | Server → Client  |
| 0x0D  | MOD_LIST          | TCP     | Server → Client  |
| 0x0E  | MOD_DATA          | TCP     | Server → Client  |
| 0x10  | PING              | UDP     | Both             |
| 0x11  | PONG              | UDP     | Both             |
| 0x17  | READY             | TCP     | Client → Server  |
| 0x18  | KICK              | TCP     | Server → Client  |
| 0x19  | BAN               | TCP     | Server → Client  |

---

## Vehicle Update Payload (PKT_VEHICLE_UPDATE = 0x08)

Total payload: **66 bytes**

```
Offset  Size  Type     Field
0       1     uint8    vehicle_id     (0-199 = player, 200-254 = AI)
1       1     uint8    vflags         (VF_IS_AI=0x01, VF_SLEEPING=0x02)
2       12    float32  position       x, y, z  (meters, world space)
14      16    float32  rotation       x, y, z, w  (unit quaternion)
30      12    float32  velocity       x, y, z  (m/s)
42      12    float32  angular_vel    x, y, z  (rad/s)
54      4     uint8    wheel[0..3]    packed throttle (× 0.004 = 0.0-1.02)
58      1     uint8    lights         LightFlags bitmask
59      1     int8     gear           -1=reverse, 0=neutral, 1-8=drive
60      4     float32  rpm
64      2     uint16   health         0=destroyed, 65535=pristine
```

AI vehicles use `vflags |= VF_IS_AI` and `sender_id = 0xFFFF`.
Vehicle IDs 200-254 are exclusively reserved for server-side AI.

---

## AUTH_REQUEST Payload (JSON)

```json
{
  "username":        "PlayerName",
  "token":           "JWT from master server (optional)",
  "server_password": "if server requires one",
  "version":         "1.0.0"
}
```

## AUTH_RESPONSE Payload (JSON)

```json
{ "ok": true, "player_id": 3, "map": "gridmap_v2" }
```

On failure:
```json
{ "ok": false, "error": "Server full" }
```

## VEHICLE_SPAWN Payload (JSON)

```json
{ "player_id": 3, "vehicle_id": 0, "model": "etk800", "config": "{}" }
```

---

## Bandwidth Budget (100 Hz, 32 players, 20 AI)

| Source                          | KB/s per client |
|---------------------------------|-----------------|
| 32 player vehicle updates       | ~237            |
| 20 AI vehicle updates (20 Hz)   | ~30             |
| TCP overhead (chat, events)     | <1              |
| **Total outbound from server**  | **~268 KB/s**   |

Spatial interest management (planned v1.1) will reduce this significantly
by only sending vehicles within a configurable distance.
