# Network System Specification

**Status**: Draft
**Phase**: 4
**Scope**: UDP multiplayer stack for MMORPG-scale games — from raw transport to high-level replication
**Inspired by**: UE5 NetDriver/IRIS analysis, ToS-Server-5 (ChaCha20/NanoSockets/FlatBuffer), netc compression

---

## 1. Design Philosophy

### Why Not Use Unreal Engine's Networking

UE networking is well-understood, well-documented, and broadly used. We studied it deeply precisely to know what *not* to replicate. The table below contrasts UE behaviour with UzEngine's design target:

| Concern | Unreal Engine | UzEngine |
|---|---|---|
| Replication granularity | Per-property dirty tracking via `memcmp` on `Recent[]` shadow buffer | Per-component snapshot diff with explicit field mask |
| RPC serialisation | Reflection-based at runtime; `UPROPERTY` overhead on hot path | Compile-time macro expansion → direct `UzBitStream` reads/writes, zero reflection |
| Saturation handling | `IsNetReady()` blocks **all** replication for a connection when any data is queued; cascade to 45–60 s stalls observed | Per-channel flow control; only the saturated channel pauses; other channels continue |
| Reliable buffer | Linked list of `FOutBunch`; 256-bunch cap; overflow = connection drop | Ring buffer 128 entries; overflow = back-pressure signal, never silent drop |
| Relevancy check | Virtual `IsNetRelevantFor()` called every tick per actor per connection → O(actors × connections) | Spatial grid with cell subscriptions; relevancy checked on cell move, not every tick |
| Bandwidth budget | `DesiredTickRate`-gated windows; all actors compete in sorted priority list | Per-connection send budgets updated via RTT/bandwidth estimator; entities emit deltas independently |
| Ordering guarantees | Single reliable channel per actor; all reliable bunches ordered globally per actor | 4 independent channels per connection; ordering enforced only within each channel |
| Dynamic array replication | `NetDeltaSerialize` with `ReplicationID/Key` tracking per element | Explicit diff + indices; no hidden key-tracking overhead |
| Blueprint RPC cost | Blueprint VM + reflection + parameter marshalling | C only; Lua RPCs use a thin trampoline, not reflected |
| IRIS | Opt-in UE5 system; 31% improvement vs legacy but still allocates 524 KB/frame in buffers | No IRIS equivalent needed; design avoids the root allocations |
| Max tested concurrent players (single process) | ~1 000 with heavy tuning | Target 5 000 per zone shard |

### Core Design Tenets

1. **Batch first, diff always.** State is sent as deltas against acknowledged snapshots. RPCs are the exception, not the rule.
2. **No reflection on the hot path.** All serialisation code is generated at compile time from C macros. The runtime sees nothing but `memcpy`-friendly structs and bit-field writes.
3. **Independent channels.** Reliable RPC congestion never blocks position updates. Position update loss never stalls chat messages.
4. **Compression is mandatory.** Every state-sync payload passes through netc before encryption. Compression is skipped only if it would increase size.
5. **Server is authoritative, always.** Clients send inputs and intents. The server simulates and replicates results.
6. **Horizontal scale is a first-class constraint.** Entity IDs encode shard index. Cross-shard calls are normal, not exceptional.
7. **Data lives outside the binary.** All game content (items, monsters, skills, spawn points, quests) is served from the Content API and hot-reloadable without rebuilding the client. The client binary changes only when engine code or compiled shaders change. See `live-content.md`.
8. **Memory is the primary database.** All read paths hit in-memory tables loaded at startup. SQLite (or SpacetimeDB) is the async write-path persistence backup only — game logic never blocks on a DB write. See `live-content.md §4`.
9. **Spells are data, not code.** Most skills can be authored in JSON with no Lua and no client rebuild. New skills are live in < 5 s via Content API. See `spell-system.md`.

---

## 2. Stack Overview

```
┌──────────────────────────────────────────────────────────────────────┐
│                         Game Plugin (game.dll / uz_server plugin)    │
│   Zone logic · combat · quest · AI · economy · persistence          │
├──────────────────────────────────────────────────────────────────────┤
│                       UzNet High-Level API                           │
│   RPC macros · replica descriptors · Lua net API · interest manager │
├──────────────────────────────────────────────────────────────────────┤
│                     Replication & Snapshot Layer                     │
│   per-component field-mask delta · priority queue · send budget     │
├────────────────────────┬─────────────────────────────────────────────┤
│   Channel Manager      │  Session / Connection Manager               │
│   UNRELIABLE           │  handshake · crypto · heartbeat            │
│   RELIABLE_ORD         │  connection ID · replay window             │
│   RELIABLE_UNO         │                                             │
│   STATE_SYNC           │                                             │
├────────────────────────┴─────────────────────────────────────────────┤
│                         Packet Pipeline                              │
│   serialise → delta-encode → netc compress → ChaCha20 encrypt → UDP │
│   UDP → decrypt → decompress → reconstruct → dispatch               │
├──────────────────────────────────────────────────────────────────────┤
│              UDP Socket (NanoSockets / platform socket)              │
│              SO_REUSEPORT · one I/O thread per core                  │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 3. Packet Wire Format

### 3.1 Header Layout

```
Byte  0      MAGIC  (0xUZ = 0x555A)  — sync word, reject non-UzNet datagrams
Byte  1      FLAGS  — bitmask (see §3.2)
Bytes 2–3    SEQUENCE — uint16, per-channel send counter (wraps at 65535)
Bytes 4–7    CONNECTION_ID — uint32, opaque; 6-bit shard + 26-bit local
Bytes 8–9    ACK_SEQ — uint16, highest received sequence on this channel
Bytes 10–13  ACK_BITS — uint32, selective-ACK bitmask (bits 0..31 = seq-1..seq-32)
Byte  14     CHANNEL — uint8 (see §3.3)
Bytes 15–16  PAYLOAD_LEN — uint16, compressed+encrypted payload length
--- payload ---
Bytes N–N+15 POLY1305_TAG — 16-byte AEAD authentication tag (appended by libsodium)
```

Total fixed overhead: **33 bytes** (17-byte header + 16-byte AEAD tag).

> **Comparison with UE**: UE bunches carry per-property numeric ID headers (minimum 2 bytes per property) that accumulate quickly. A 10-property update costs at minimum 20 bytes of header in UE vs. a single 4-byte field-mask in UzNet covering up to 32 fields.

### 3.2 FLAGS Byte

| Bit | Name         | Meaning |
|-----|--------------|---------|
| 0   | RELIABLE     | Packet requires ACK + retransmit |
| 1   | FRAGMENT     | This datagram is one fragment of a larger message |
| 2   | LAST_FRAG    | This is the last fragment |
| 3   | COMPRESSED   | Payload was netc-compressed (flag cleared if compression harmful) |
| 4   | ENCRYPTED    | Payload is ChaCha20-Poly1305 encrypted |
| 5   | DELTA        | Payload is a delta against a known baseline snapshot |
| 6   | ORDERED      | Channel enforces in-order delivery to application |
| 7   | HEARTBEAT    | Keep-alive; no payload; PAYLOAD_LEN = 0 |

### 3.3 Channel Byte

| Value | Name           | Reliable | Ordered | Primary use |
|-------|----------------|----------|---------|-------------|
| 0     | UNRELIABLE     | No       | No      | Position, rotation, animation state (lossy OK) |
| 1     | RELIABLE_ORD   | Yes      | Yes     | Chat, inventory mutations, quest state |
| 2     | RELIABLE_UNO   | Yes      | No      | Damage events, skill casts, targeted RPCs |
| 3     | STATE_SYNC     | No       | No      | Bulk world-state snapshot (latest wins) |
| 4–15  | Game-defined   | —        | —       | Reserved for game plugin |

Each channel maintains its own sequence counter and ACK state. A back-pressure event on `RELIABLE_ORD` (e.g. chat spam) has zero effect on `UNRELIABLE` position packets.

> **UE difference**: UE uses a single reliable channel per actor (`UActorChannel`). All reliable data (properties + RPCs) competes in the same `FOutBunch` linked list. One large RPC can stall property replication for many frames.

### 3.4 Fragmentation

Payloads exceeding **1 200 bytes** (conservative MTU safe zone) are split automatically:

- Fragment header appended after standard header: `[frag_index: uint8][frag_total: uint8]`
- All fragments carry the same `SEQUENCE` as the logical packet.
- Reassembly buffer: 64 in-flight fragment groups per connection, each with 255 max slots.
- Incomplete groups are discarded after `fragment_timeout_ms` (default 2 000 ms).
- Fragment loss on `RELIABLE` channels triggers retransmit of only missing fragments (SACK-based).

---

## 4. Serialisation — `UzBitStream`

All game data is packed at bit granularity before entering the pipeline. No runtime reflection is used.

### 4.1 Primitive Encodings

| Type | Encoding | Wire size |
|---|---|---|
| `bool` | 1 bit | 1 bit |
| `uint8` | raw 8 bits | 8 bits |
| `uint16` | raw 16 bits | 16 bits |
| `uint32` | VarUInt (LEB128, 7 bits/byte) | 1–5 bytes |
| `int32` | ZigZag → VarUInt | 1–5 bytes |
| `int64` | ZigZag → VarUInt | 1–9 bytes |
| `float32` quantised | N-bit fixed-point in `[min, max]` | N bits (configurable) |
| `float32` raw | IEEE-754 | 32 bits |
| `vec3` world pos | 3 × int16 (1 cm precision, ±327 m) | 48 bits |
| `vec3` normalised | 2 × uint16 spherical (azimuth + polar) | 32 bits |
| `quat` | Smallest-three: 2-bit selector + 3 × 15-bit | 47 bits |
| `string` | VarUInt byte-length + UTF-8 bytes | variable |
| `entity_id` | uint32 packed (6-bit shard + 26-bit local) | 32 bits |
| `bitset<N>` | N bits direct | N bits |

#### ZigZag encoding

Maps signed integers to unsigned so small negative values are cheap in VarUInt:

```c
uint32_t zigzag_enc32(int32_t n)  { return ((uint32_t)n << 1) ^ (uint32_t)(n >> 31); }
int32_t  zigzag_dec32(uint32_t n) { return (int32_t)((n >> 1) ^ -(int32_t)(n & 1));  }
```

#### VarUInt (LEB128)

| Value range | Bytes used |
|---|---|
| 0–127 | 1 |
| 128–16 383 | 2 |
| 16 384–2 097 151 | 3 |
| 2 097 152–268 435 455 | 4 |
| > 268 435 455 | 5 |

#### Quantisation helpers

```c
// Encode float in [min,max] with `bits` precision
uint32_t uz_quantise  (float v, float min, float max, int bits);
float    uz_dequantise(uint32_t raw, float min, float max, int bits);

// World-space position (int16 per axis, 1 cm precision, ±327.68 m)
void uz_write_worldpos(UzBitStream* bs, float x, float y, float z);
void uz_read_worldpos (UzBitStream* bs, float* x, float* y, float* z);

// Quaternion — smallest-three encoding (47 bits)
void uz_write_quat(UzBitStream* bs, float qx, float qy, float qz, float qw);
void uz_read_quat (UzBitStream* bs, float* qx, float* qy, float* qz, float* qw);
```

### 4.2 UzBitStream API

```c
typedef struct { uint8_t* buf; uint32_t cap_bits; uint32_t pos_bits; } UzBitStream;

// Write
void uzbs_write_bits  (UzBitStream* bs, uint64_t value, int bits);
void uzbs_write_bool  (UzBitStream* bs, bool v);
void uzbs_write_u8    (UzBitStream* bs, uint8_t v);
void uzbs_write_varint(UzBitStream* bs, uint64_t v);     // VarUInt
void uzbs_write_zigzag(UzBitStream* bs, int32_t v);      // ZigZag + VarUInt
void uzbs_write_f32q  (UzBitStream* bs, float v, float min, float max, int bits);
void uzbs_write_str   (UzBitStream* bs, const char* s, uint32_t len);
void uzbs_write_bytes (UzBitStream* bs, const uint8_t* data, uint32_t len);

// Read (mirror of write)
uint64_t uzbs_read_bits  (UzBitStream* bs, int bits);
bool     uzbs_read_bool  (UzBitStream* bs);
uint8_t  uzbs_read_u8    (UzBitStream* bs);
uint64_t uzbs_read_varint(UzBitStream* bs);
int32_t  uzbs_read_zigzag(UzBitStream* bs);
float    uzbs_read_f32q  (UzBitStream* bs, float min, float max, int bits);
uint32_t uzbs_read_str   (UzBitStream* bs, char* out, uint32_t cap);
void     uzbs_read_bytes (UzBitStream* bs, uint8_t* out, uint32_t len);

// Utilities
uint32_t uzbs_byte_length(const UzBitStream* bs);   // ceil(pos_bits / 8)
bool     uzbs_overflowed (const UzBitStream* bs);
void     uzbs_reset      (UzBitStream* bs);
```

---

## 5. Delta Compression — Snapshot System

This is the primary bandwidth reduction mechanism. It replaces UE's per-property shadow-state comparison with an explicit snapshot ring and field-mask diff.

### 5.1 Snapshot Ring

Each connection maintains a **ring of 32 snapshots**. A snapshot is the full serialised state of all replicated components for all entities relevant to that connection, keyed by snapshot sequence number.

```
ring[0..31]  =  snapshot_seq → { entity_id → component_blob[] }
baseline_seq =  last snapshot ACK'd by client (extracted from packet ACK_SEQ on STATE_SYNC channel)
```

When client ACKs snapshot N, the server diffs current state against snapshot N to produce the next delta.

### 5.2 Delta Packet Layout

```
[baseline_seq : uint16]          // 0xFFFF = no baseline (send full state)
[entity_count : varuint]
for each entity:
  [entity_id   : uint32]
  [comp_mask   : uint16]         // one bit per component type (up to 16 components)
  for each set bit in comp_mask:
    [field_mask  : uint32]       // one bit per field in that component (up to 32 fields)
    [field_data  : variable]     // only fields with changed bits, in field index order
```

If `baseline_seq = 0xFFFF` (no ack or too stale), all fields are sent regardless of field_mask (full snapshot).

> **Why not UE's approach**: UE compares a flat `Recent[]` shadow buffer via `memcmp` per property per connection. For 5 000 connections × 200 visible entities × 20 properties = 20 million comparisons per tick at 20 Hz. UzEngine's field_mask approach reduces this to computing one XOR per 32-field group.

### 5.3 Replication Descriptor

Each replicated ECS component registers a `UzReplicaDesc` once at startup:

```c
typedef enum {
    UZENC_BOOL,      // 1 bit
    UZENC_U8,        // 8 bits
    UZENC_U16,       // 16 bits
    UZENC_VARUINT,   // VarUInt
    UZENC_ZIGZAG,    // ZigZag + VarUInt
    UZENC_QUANTF32,  // quantised float (uses quant_min/max/bits)
    UZENC_RAWF32,    // IEEE-754 float
    UZENC_WORLDPOS,  // 3 × int16 world position
    UZENC_QUAT,      // smallest-three quaternion
    UZENC_STRING,    // VarUInt length + UTF-8
    UZENC_BYTES,     // VarUInt length + raw bytes
} uz_enc_type;

typedef struct {
    const char* name;
    uint32_t    offset;       // byte offset within the component struct
    uint32_t    size;         // byte size of the field (for memcmp fast-path)
    uz_enc_type encoding;
    float       quant_min;   // UZENC_QUANTF32 only
    float       quant_max;
    uint8_t     quant_bits;
    bool        important;   // if true, force-include even if below priority threshold
} uz_replica_field_t;

typedef struct {
    const char*          component_name;
    uz_replica_field_t*  fields;
    uint32_t             field_count;
    uint32_t             priority;          // replication priority (higher = sent first)
    float                relevance_range;   // 0 = always relevant
    uint8_t              send_channel;      // default CHANNEL_STATE_SYNC
} uz_replica_desc_t;

// Register at startup
void uz_replica_register(const uz_replica_desc_t* desc);
```

### 5.4 Priority Queue & Send Budget

Each connection has a **send budget** in bits per tick, derived from a bandwidth estimate (RTT + packet loss → AIMD-like increase/decrease). Entities are replicated in priority order until the budget is exhausted.

Priority = `replica_desc.priority × distance_weight × time_since_last_send`

Entities not replicated this tick carry forward increased priority (starvation prevention).

> **UE comparison**: UE's `ServerReplicateActors_PrioritizeActors()` also uses a priority sort, but the saturation check (`IsNetReady()`) blocks *all* actors for a connection once any data is queued. UzEngine tracks budget separately per channel; STATE_SYNC budget exhaustion never stalls RELIABLE_UNO.

---

## 6. Compression — netc Integration

After delta encoding and serialisation, all payloads are optionally compressed with **netc** before encryption.

### 6.1 netc Internals (Summary)

netc uses multi-technique competition per packet:
- **tANS (Finite State Entropy)** entropy coder with 12-bit/10-bit adaptive tables
- **LZP prediction pre-filter** (XOR with position-aware context → frequent 0x00 runs)
- **Bigram-PCTX context model** per byte position
- **Inter-packet delta prediction** with order-2 linear extrapolation
- Competition between tANS, LZ77, RLE, and passthrough — smallest wins

On typical 32–512 byte game packets: compression ratio 0.33–0.50, throughput 32–63 MB/s.

### 6.2 Integration API

```c
// One context per connection — holds per-connection adaptive tables
typedef struct uz_netc_ctx_s uz_netc_ctx_t;
uz_netc_ctx_t* uz_netc_ctx_create(void);
void           uz_netc_ctx_destroy(uz_netc_ctx_t* ctx);
void           uz_netc_ctx_reset  (uz_netc_ctx_t* ctx);   // on reconnect

// Returns compressed size, or 0 if compression is not beneficial
size_t uz_netc_compress  (uz_netc_ctx_t* ctx,
                           const uint8_t* src, size_t src_len,
                           uint8_t* dst, size_t dst_cap);

// Returns decompressed size, or 0 on error
size_t uz_netc_decompress(uz_netc_ctx_t* ctx,
                           const uint8_t* src, size_t src_len,
                           uint8_t* dst, size_t dst_cap);
```

The `COMPRESSED` flag in the packet header is set only when `compressed_size < original_size`. Otherwise the raw payload is forwarded and the flag is cleared — zero overhead path.

---

## 7. Encryption — ChaCha20-Poly1305

All payloads are encrypted with **ChaCha20-Poly1305 IETF** (RFC 8439) via libsodium. The 16-byte authentication tag is appended to the payload (already included in `PAYLOAD_LEN`).

### 7.1 Key Exchange — X25519 ECDH + HKDF

```
Client                                Server
  │                                      │
  │ generate ephemeral (c_priv, c_pub)   │
  │──── HELLO [c_pub (32B), c_nonce (24B)] ──>│
  │                                      │  generate ephemeral (s_priv, s_pub)
  │<─── HELLO_ACK [s_pub (32B), conn_id] ────│
  │                                      │
  │  shared = X25519(c_priv, s_pub)      │  shared = X25519(s_priv, c_pub)
  │  session_key = HKDF-SHA256(          │
  │      IKM=shared, salt=c_nonce,       │
  │      info="uzengine-net-v1", L=32)   │
  │                                      │
  │──── HANDSHAKE_OK [HMAC-SHA256 of session_key + conn_id] ──>│
  │<─── SESSION_ACK ─────────────────────│
  │  (now all packets encrypted)         │
```

- Nonce per packet: `session_nonce_base XOR (uint64_t)sequence_number` — prevents nonce reuse.
- Both sides derive identical `session_key` from the DH shared secret.
- HMAC in HANDSHAKE_OK proves the client completed the DH correctly (anti-replay on handshake).

### 7.2 Replay Protection

A **64-position sliding window** bitset per connection:

- Tracks the 64 most recently seen sequence numbers.
- Packets with sequence ≤ `(highest_seen − 64)` are rejected.
- Duplicate sequence numbers within the window are rejected.
- Window advances whenever a new `highest_seen` is established.

### 7.3 DDoS Mitigation — Stateless Cookie

Before assigning a `CONNECTION_ID`, the server responds to HELLO with a stateless HMAC-SHA256 cookie:

```
cookie = HMAC-SHA256(server_secret, client_ip || client_port || timestamp_window)
```

The client reflects the cookie in a COOKIE_ACK packet. The server verifies it without any per-client state. Only after verification is a `CONNECTION_ID` allocated and memory committed.

---

## 8. Reliability Layer

### 8.1 Selective ACK (SACK)

The `ACK_SEQ` + `ACK_BITS` fields in every packet header implement SACK:

- `ACK_SEQ` = highest in-order sequence number received on this channel.
- `ACK_BITS` = 32-bit mask; bit `i` = 1 means `ACK_SEQ − (i+1)` was also received.
- On the send side, any sequence number not set in the ACK window after `RTO` milliseconds is retransmitted.

### 8.2 Send Ring Buffer

Per-connection, per-channel ring buffer of 128 entries:

```c
typedef struct {
    uint8_t  data[UZ_MAX_PACKET];
    uint16_t len;
    uint16_t seq;
    uint64_t sent_at_ns;   // timestamp for RTT measurement
    uint8_t  retransmits;  // drop connection after UZ_MAX_RETRANSMIT (8)
} uz_send_entry_t;
```

Buffer full = back-pressure: new reliable sends are queued in an application-level overflow queue (up to 4 096 entries) and drained as slots open. This replaces UE's "connection drop on buffer overflow" behaviour with a graceful stall.

### 8.3 RTT & RTO Estimation

EWMA (Jacobson/Karels algorithm):

```
RTTVAR = (1 - β) × RTTVAR + β × |SRTT - RTT_sample|    (β = 0.25)
SRTT   = (1 - α) × SRTT   + α × RTT_sample             (α = 0.125)
RTO    = SRTT + 4 × RTTVAR
RTO    = clamp(RTO, 50ms, 2000ms)
```

Karn's algorithm: RTT samples are not taken from retransmitted packets.

### 8.4 Ordered Delivery (RELIABLE_ORD)

The `RELIABLE_ORD` channel holds an **out-of-order receive queue** of 64 slots. Packets are delivered to the application only in monotonically increasing sequence number order. Gaps stall delivery until the missing packet arrives (or is retransmitted).

> **UE comparison**: UE enforces ordering globally per actor via its `FInBunch` processing. A single out-of-order reliable bunch blocks all processing for that actor. UzEngine's ordering scope is the `RELIABLE_ORD` channel only — other channels are unaffected.

---

## 9. NAT Traversal

```
Client A                 Rendezvous Server              Client B / Dedicated Server
   │                           │                               │
   │── STUN request ──>  [STUN server]                        │
   │<─ public ip:port ─────────│                               │
   │                           │                               │
   │── REGISTER(pub_addr) ─────>                               │
   │                           │<────── REGISTER(pub_addr) ───│
   │<── PEER_INFO(B.pub_addr) ─│                               │
   │                           │──────── PEER_INFO(A.pub_addr) >│
   │                           │                               │
   │── UDP punch (to B) ───────────────────────────────────────>│
   │<──────────────────────────────────────── UDP punch (to A) ─│
   │                           │                               │
   │ (connection established if either punch reaches)          │
   │                           │                               │
   │ (fallback after 3s: route through TURN relay)             │
```

1. **STUN** (RFC 5389) — discover public IP:port.
2. **UDP Hole Punch** — both sides send simultaneously to open NAT mappings.
3. **TURN relay** — transparent fallback if symmetric NAT blocks direct punch.

Client API hides all of this:

```c
uz_connection* uz_net_connect(const char* host, uint16_t port, uz_connect_flags flags);
// flags: UZ_CONN_STUN | UZ_CONN_TURN_FALLBACK | UZ_CONN_SKIP_ENCRYPTION (LAN debug)
```

---

## 10. Session & Connection Manager

### 10.1 Connection State Machine

```
DISCONNECTED
    │  uz_net_connect()
    ▼
CONNECTING          ← STUN / HELLO sent
    │  HELLO_ACK received
    ▼
HANDSHAKING         ← X25519 in progress
    │  SESSION_ACK received
    ▼
CONNECTED           ← normal operation
    │  uz_net_disconnect() or timeout
    ▼
DISCONNECTING       ← GOODBYE sent, waiting final ACK
    │  ACK or timeout
    ▼
DISCONNECTED
```

Any state can transition directly to `DISCONNECTED` on fatal error (AEAD failure, retransmit exhaustion, etc).

### 10.2 Connection ID Format

```
 31    26 25                0
 ┌──────┬─────────────────────┐
 │SHARD │    LOCAL INDEX      │
 │ 6b   │        26b          │
 └──────┴─────────────────────┘
```

- 64 shards × 67 M local IDs per shard.
- Cross-shard entity references use the full 32-bit ID; the routing layer knows which shard process owns each shard index.

### 10.3 Heartbeat

- `HEARTBEAT` packet (FLAGS = 0x80, PAYLOAD_LEN = 0) sent if no other packet was sent within `heartbeat_interval_ms` (default 1 000 ms).
- Connection dropped after `connection_timeout_ms` (default 10 000 ms) of silence.

---

## 11. RPC System

> **Key difference from UE**: UE's RPC mechanism uses `UFunction` reflection at runtime — every call walks the property list, marshals parameters through a `FArchive`, and dispatches through the Blueprint VM if the function is Blueprint-callable. This costs ~2–5 µs per RPC call in C++, and 10–50 µs in Blueprint. For a 5 000-player server sending combat events at 20 Hz that is a non-trivial CPU budget.
> UzEngine RPCs expand entirely at compile time. The runtime cost is a function call + a `UzBitStream` write.

### 11.1 RPC Declaration Macros

Declared in a shared header included by both client and server:

```c
// Server → Client (broadcast to all, or targeted)
UZ_RPC_S2C(SpawnEntity,
    (UZ_U32,     entity_id)
    (UZ_U8,      entity_type)
    (UZ_WPOS,    pos)             // world position (48 bits)
    (UZ_U16,     template_id)
    (UZ_BOOL,    is_visible)
)

// Client → Server
UZ_RPC_C2S(PlayerInput,
    (UZ_F32Q_8,  move_forward)    // quantised float, 8 bits, [-1,1]
    (UZ_F32Q_8,  move_right)
    (UZ_F32Q_16, aim_yaw)         // 16-bit quantised yaw
    (UZ_F32Q_16, aim_pitch)
    (UZ_U8,      action_flags)
)
```

Macro-expanded output (conceptually):

```c
// Auto-generated send function (server side)
void send_SpawnEntity(uz_connection* conn, uint32_t entity_id, uint8_t entity_type,
                       uz_vec3 pos, uint16_t template_id, bool is_visible) {
    uint8_t buf[UZ_RPC_MAX_SIZE];
    UzBitStream bs = uzbs_init(buf, sizeof(buf));
    uzbs_write_u8  (&bs, OPCODE_SpawnEntity);   // auto-assigned from rpc_registry.h
    uzbs_write_u32 (&bs, entity_id);            // varuint
    uzbs_write_u8  (&bs, entity_type);
    uz_write_worldpos(&bs, pos.x, pos.y, pos.z);
    uzbs_write_u16 (&bs, template_id);
    uzbs_write_bool(&bs, is_visible);
    uz_channel_send(conn, CHANNEL_RELIABLE_UNO, &bs);
}

// Auto-generated dispatch entry (registered in rpc_registry at startup)
// on_SpawnEntity is implemented by the game plugin
```

### 11.2 Opcode Registry

`rpc_registry.h` assigns a 2-byte opcode to each RPC at compile time using an X-macro table. This avoids runtime registration and ensures server and client always agree on opcodes.

```c
// rpc_registry.h  — add rows here; never reorder or delete (use tombstones)
#define UZ_RPC_TABLE(X)                       \
    X(0x0001, SpawnEntity)                    \
    X(0x0002, DespawnEntity)                  \
    X(0x0003, PlayerInput)                    \
    X(0x0004, ChatMessage)                    \
    X(0x0005, DamageEvent)                    \
    /* 0x0006 TOMBSTONE — was MoveEntity */    \
    X(0x0007, InventoryUpdate)                \
    /* ... */
```

### 11.3 Lua RPC Trampoline

Lua game scripts can send and receive RPCs through a thin C trampoline — no reflection, no extra allocation:

```lua
-- Client
ctx.net:send_c2s("PlayerInput", {
    move_forward = 1.0,
    move_right   = 0.0,
    aim_yaw      = 180.0,
    aim_pitch    = -10.0,
    action_flags = 0x03,
})

-- Server plugin (Lua)
ctx.net:on_c2s("PlayerInput", function(conn, data)
    local entity = get_player_entity(conn)
    entity:apply_input(data)
end)
```

The trampoline maps Lua table fields to `UzBitStream` writes using the same field descriptor table as the C macro. No dynamic dispatch or type inspection at runtime.

---

## 12. Interest Management (Area of Interest)

### 12.1 Design Rationale — Classic vs Megaserver

UzEngine adopts the **classic fixed-capacity, no-shard model** (Lineage 2 / Ultima Online style) rather than the megaserver (GW2 / ESO) or dynamic-layer (WoW CRZ) model. The primary driver is economy integrity:

| Model | Rare item supply | Economy stability | Implementation complexity |
|---|---|---|---|
| Megaserver (GW2/ESO) | Multiplied by number of instances → inflation | Broken without gear treadmill | High |
| Layering/CRZ (WoW) | Per-realm duplication × realms → dilution | Partially broken | High |
| Fixed single-realm (L2/UO) | Exactly one per spawn timer | Player-driven scarcity | Low |

A single rare NPC on a fixed-capacity server means exactly one rare item in the economy at that moment. Players compete for it. That scarcity is the foundation of a healthy MMORPG economy.

There are **no shards, no layers, no cross-realm zones**. Each game world is one physical server process with a hard player capacity (configurable, default 3 000). When the realm fills, new connections are queued. Multiple separate realms (each with its own isolated economy) can exist, but they share no game state.

### 12.2 Spatial Grid (AoI)

The world is divided into a **uniform grid of cells** per zone:

```
cell_size_m = 100    (default; configurable per zone)
```

Entities are registered in exactly one cell based on their world position. Cell membership is updated **only on move**, not every tick. Hysteresis prevents border flapping:

- Enter new cell: position crosses the cell boundary by `enter_threshold_m` (default 0 m, immediate).
- Leave old cell: position is `leave_hysteresis_m` (default 10 m) past the boundary in the new cell direction.

### 12.3 Interest Set Per Connection

Each connection subscribes to a square ring of cells around the player:

```c
typedef struct {
    int32_t  cx, cy;                          // player's current cell
    uint8_t  ring;                            // interest ring radius (default 2 → 5×5 = 25 cells)
    uint32_t subscribed[MAX_ZONE_CELLS / 32]; // bitset over all zone cells
} uz_interest_t;
```

On cell transition:

1. Compute new ring set (5×5 around new cell).
2. XOR with old subscribed set → cells entering scope, cells leaving scope.
3. For **leaving** cells: send `DespawnEntity` RPCs for all entities in those cells.
4. For **entering** cells: send `SpawnEntity` RPCs for all entities in those cells.
5. Update bitset.

The replication system checks entity cell membership against the connection's subscribed bitset with a single bit test before generating any packet. Entities outside interest generate zero traffic.

> **Why not UE's approach**: UE calls `IsNetRelevantFor()` once per actor per connection per tick — 2.5 M virtual function calls per tick at 20 Hz for 5 000 connections × 500 visible actors. UzEngine updates the bitset once on entity move and does a bit test per replication decision. The hot path is a `(subscribed[cell / 32] >> (cell & 31)) & 1` check.

### 12.4 Interest LOD

Entities in the outermost ring receive reduced replication rate and lower position precision — reducing bandwidth to distant entities without removing awareness:

| Ring distance | Replication rate | Position precision |
|---|---|---|
| Ring 0 (same cell) | 20 Hz | 1 cm (int16) |
| Ring 1 | 10 Hz | 1 cm |
| Ring 2 (outer) | 5 Hz | 10 cm (int14) |
| Beyond interest | 0 Hz | N/A (despawned on client) |

### 12.5 Broadcast Optimisation

When a state update is relevant to many connections in the same area, the server **broadcasts one packet to a cell group** rather than N identical per-connection packets. Each broadcast packet is encrypted with a **group session key** shared by all subscribers of that cell group, eliminating per-connection encryption overhead for dense areas.

---

## 13. Server Architecture (`uz_server`)

### 13.1 Topology — Classic Realm Model

```
Players ──────────────────────────────────────────────────┐
                                                          ▼
                                               ┌──────────────────┐
                                               │   Login Server   │
                                               │  auth · account  │
                                               │  char select     │
                                               └────────┬─────────┘
                                                        │ authenticated session token
                                    ┌───────────────────┼────────────────────┐
                                    ▼                   ▼                    ▼
                          ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
                          │  Realm "Aden"│    │  Realm "Giran│    │  Realm "N"   │
                          │  uz_server   │    │  uz_server   │    │  uz_server   │
                          │  cap: 3 000  │    │  cap: 3 000  │    │  cap: 3 000  │
                          └──────┬───────┘    └──────────────┘    └──────────────┘
                                 │  (one process, no shards)
                    ┌────────────┴──────────────────────┐
                    │        Zone Threads (per zone)     │
                    │  "Talking Island" ── 20 Hz tick    │
                    │  "Forest of Mist" ── 20 Hz tick    │
                    │  "Dungeon A-1"    ── 20 Hz tick    │
                    │  …                                 │
                    └────────────┬──────────────────────┘
                                 │
                    ┌────────────▼──────────────────────┐
                    │         In-Memory Tables           │
                    │  players · items · spawns · zones  │
                    └────────────┬──────────────────────┘
                                 │ async write queue
                    ┌────────────▼──────────────────────┐
                    │         Persistence Backend        │
                    │   SQLite (current) / SpacetimeDB  │
                    └───────────────────────────────────┘
```

**No layers, no shards, no cross-realm zones.** The realm is one world. Full stop.

### 13.2 Thread Model

The key lesson from Lineage 2's architecture and from general concurrency research: **multiple threads sharing the same memory bus compete for bandwidth**. The most scalable pattern is **process-per-zone** (each zone on its own machine). For a single `uz_server` process hosting multiple zones, threads are isolated to prevent cross-zone contention:

```
uz_server process
│
├── I/O thread pool  (one thread per CPU core, SO_REUSEPORT)
│     recv datagram → decrypt → identify zone → push to zone inbound queue
│     drain zone outbound queues → encrypt → send datagram
│     (never touches game state)
│
├── Zone thread: "Talking Island"
│     owns: entity table, AoI grid, spawn manager, navmesh query
│     tick at 20 Hz:
│       1. drain inbound RPC queue (lock-free MPSC)
│       2. advance physics / AI / ability timers
│       3. run plugin on_zone_tick()
│       4. collect replication deltas → push to outbound queues
│     (never touches socket)
│
├── Zone thread: "Forest of Mist"
│     … (identical structure, owns its own data)
│
└── Shared read-only data (no locks needed):
      content catalogue (ItemDef, MonsterDef, SkillDef)
      navmesh binary (.uznavmesh)
      zone config
```

Zone threads share **no mutable state**. The only crossing points are:

- **Inbound queue** (I/O → Zone): lock-free MPSC ring, 65 536 entries.
- **Outbound queue** (Zone → I/O): lock-free MPSC ring per connection.
- **Zone transfer**: player moving between zones sends a token through an internal TCP pipe between zone threads; no shared memory.

### 13.3 Realm Capacity and Queue

Each realm has a configured `max_connections` (default 3 000). Connections beyond this limit are placed in a **login queue**:

```c
typedef struct {
    uint32_t max_connections;      // hard cap
    uint32_t queue_max;            // max queued connections (default 500)
    uint32_t queue_tick_interval_ms; // how often to admit from queue (default 5 000)
    uint32_t queue_admit_per_tick;   // connections let in per tick (default 1)
} uz_realm_config_t;
```

The client displays a queue position and estimated wait time. The server does not drop established connections to make room for queued players.

### 13.4 Zone Transfer

When a player walks through a zone boundary (hard transition, loading screen):

```
1. Zone A thread marks connection as TRANSFERRING, stops replication
2. Zone A serialises player state → auth token (msgpack, signed with server secret)
3. Zone A sends Transfer RPC to client: { zone_b_id, token, zone_b_udp_addr }
4. Client opens new UDP connection to zone B's address (may be same process, different port)
5. Zone B thread validates token, deserialises player state, begins replication
6. Zone A thread releases the connection slot
```

If zone A and zone B are on the same `uz_server` process, the token is passed through an internal pipe (no network hop). If they are on separate machines, the token travels over an authenticated internal TCP channel.

### 13.5 Game Plugin Interface — Stable C ABI

```c
// uz_server_plugin.h  — never break this ABI; new entries appended at the end only

typedef struct uz_server_plugin_s {
    uint32_t abi_version;   // UZ_SERVER_PLUGIN_ABI_VERSION

    // Lifecycle
    void (*on_init)      (uz_server_ctx* ctx);
    void (*on_shutdown)  (uz_server_ctx* ctx);

    // Per-zone fixed tick (called at 20 Hz from zone worker thread)
    void (*on_zone_tick) (uz_zone* zone, double delta_s);

    // Connection events
    void (*on_connect)   (uz_zone* zone, uz_connection* conn);
    void (*on_disconnect)(uz_zone* zone, uz_connection* conn, uz_disconnect_reason reason);
    void (*on_zone_transfer_in) (uz_zone* zone, uz_connection* conn, const uint8_t* state, uint32_t state_len);
    void (*on_zone_transfer_out)(uz_zone* zone, uz_connection* conn, uint8_t* state, uint32_t* state_len);

    // RPC dispatch — all C2S RPCs arrive here
    void (*on_rpc)(uz_zone* zone, uz_connection* conn, uint16_t opcode, UzBitStream* bs);
} uz_server_plugin_t;

UZ_EXPORT uz_server_plugin_t* uz_plugin_get(void);
```

### 13.6 Server-Side Lua API

```lua
-- Zone tick
ctx.net:on_zone_tick(function(zone, dt)
    for conn in zone:connections() do
        local entity = conn:get_entity()
        if entity then entity:tick_movement(dt) end
    end
end)

-- C2S RPC
ctx.net:on_c2s("PlayerInput", function(zone, conn, data)
    conn:get_entity():queue_input(data)
end)

-- Admin command
ctx.net:on_c2s("AdminSpawnCreate", function(zone, conn, data)
    if not conn:has_role("admin") then return end
    zone:spawn_manager():create(data)
end)
```

---

## 14. Client Integration — ECS Components

| Component | Description | Channel |
|---|---|---|
| `CNetworkIdentity` | Marks entity as network-replicated; holds `entity_id` and `owner_connection_id` | — |
| `CNetworkTransform` | Replicates position + rotation + scale | `STATE_SYNC` (UNRELIABLE, delta) |
| `CNetworkAnimState` | Replicates animation state machine index + blend weights | `UNRELIABLE` |
| `CNetworkHealth` | Replicates HP, max HP, shield values | `STATE_SYNC` |
| `CNetworkOwnership` | Determines which connection is the authoritative owner | — |

The `NetworkManager` singleton system drives the connection lifecycle and dispatches incoming RPCs on each ECS tick.

### 14.1 Client Prediction & Reconciliation

The client runs a **local prediction simulation** for the player character to hide input latency:

1. Client applies input locally (tentative state).
2. Client sends `PlayerInput` RPC to server.
3. Server simulates authoritatively and sends state snapshot.
4. Client receives authoritative state; if it differs from predicted state beyond threshold → **reconcile**: rewind to last confirmed server state and re-simulate inputs from that point.

This is a standard client-side prediction / server reconciliation (CSPR) model, similar to Overwatch's and Quake's approach.

### 14.2 Entity Interpolation

Remote entities (other players, NPCs) are rendered using **interpolation** between the two most recent received snapshots with a fixed **interpolation delay** (default 100 ms = 2 state-sync ticks at 20 Hz). This smooths out jitter from irregular packet arrival.

---

## 15. Security Model

| Threat | Mitigation |
|---|---|
| Eavesdropping | ChaCha20-Poly1305 AEAD on all payloads after handshake |
| Packet tampering | AEAD tag — any byte change causes authentication failure and silent drop |
| Packet replay | 64-position sliding window per connection, per channel |
| DDoS amplification | Stateless HMAC-SHA256 cookie before connection ID allocation |
| Key exchange MitM | X25519 ephemeral ECDH — forward secrecy; each session has unique keys |
| Speed / teleport hacking | Server-authoritative simulation; client position not trusted |
| Packet injection | Connection ID is opaque; attacker cannot forge valid encrypted+authenticated packets |
| Flood (pre-handshake) | Rate limit by IP before HELLO is processed (token bucket, 10 HELLO/s/IP) |
| Brute-force key exchange | X25519 — 128-bit security level; computationally infeasible |
| RPC abuse | Server validates all input; input range is enforced in `on_rpc` handler |

---

## 16. Performance Targets

| Metric | Target |
|---|---|
| Max concurrent connections per shard | 5 000 |
| Zone simulation tick rate | 20 Hz (50 ms tick) |
| State-sync send rate (per client) | 5–30 Hz adaptive (Interest LOD) |
| RPC send latency (RELIABLE_UNO) | < 0.5 µs CPU cost per call |
| Compression ratio (state sync, 10% field change) | ≤ 0.45 |
| Full snapshot size (200 visible entities, CTransform only) | < 4 KB |
| Memory per connection (server) | < 8 KB |
| CPU for 1 000 connected clients at 20 Hz | < 1 CPU core |
| Handshake time (LAN) | < 5 ms |
| Handshake time (WAN, no STUN needed) | < 100 ms |

---

## 17. File Layout

```
src/engine/net/
├── uz_net.h                        — public API (client + server)
├── uz_net.c                        — init / shutdown / main loop
│
├── socket/
│   ├── udp_socket.h/.c             — platform UDP + SO_REUSEPORT
│   └── nat_traversal.h/.c          — STUN / hole punch / TURN
│
├── session/
│   ├── connection.h/.c             — state machine, lifecycle
│   ├── session_manager.h/.c        — connection pool, ID assignment
│   └── heartbeat.h/.c              — keep-alive
│
├── transport/
│   ├── packet.h/.c                 — header encode/decode, wire layout
│   ├── fragment.h/.c               — fragmentation + reassembly
│   ├── reliable.h/.c               — SACK, RTO, retransmit ring
│   └── window.h/.c                 — sliding window replay protection
│
├── crypto/
│   ├── handshake.h/.c              — X25519 ECDH + HKDF session key
│   ├── chacha20.h/.c               — ChaCha20-Poly1305 encrypt/decrypt (libsodium)
│   └── cookie.h/.c                 — stateless HMAC cookie anti-DDoS
│
├── serial/
│   ├── bitstream.h/.c              — UzBitStream primitives
│   ├── varint.h/.c                 — VarUInt (LEB128) + ZigZag
│   └── quantise.h/.c              — float quantisation + worldpos + quat
│
├── compress/
│   └── netc_bridge.h/.c            — uz_netc_ctx lifecycle + compress/decompress
│
├── delta/
│   ├── snapshot.h/.c               — snapshot ring, baseline tracking per connection
│   └── delta_encode.h/.c           — field-mask diff encode/decode
│
├── replication/
│   ├── replica_desc.h/.c           — component descriptor registration + registry
│   ├── interest_manager.h/.c       — zone grid, cell subscription, LOD
│   └── priority_queue.h/.c         — entity send-priority heap + budget accounting
│
├── rpc/
│   ├── rpc_registry.h              — X-macro opcode table (shared client + server)
│   ├── rpc_macros.h                — UZ_RPC_S2C / UZ_RPC_C2S expansion
│   └── rpc_dispatch.h/.c           — opcode → handler vtable dispatch
│
└── server/
    ├── uz_server.h/.c              — server process entry point, I/O thread pool
    ├── zone.h/.c                   — zone worker thread, tick loop, inbound queue
    ├── connection_router.h/.c      — gateway shard routing
    └── uz_server_plugin.h          — stable C ABI for game logic DLL

tests/net/
├── test_bitstream.c                — read/write round-trip, overflow, edge values
├── test_varint.c                   — ZigZag + VarUInt correctness for all int ranges
├── test_quantise.c                 — float, worldpos, quat round-trip error bounds
├── test_delta.c                    — snapshot diff, baseline stale, full-send fallback
├── test_fragment.c                 — 1B / 1200B / 65535B frag + reassembly + timeout
├── test_reliable.c                 — SACK, RTO, 10% packet loss, ordered delivery
├── test_window.c                   — replay window accept / reject / wrap-around
├── test_crypto.c                   — RFC 8439 test vectors, handshake, cookie
├── test_interest.c                 — cell subscription, ring diff, LOD rate
└── test_rpc.c                      — macro expansion, opcode registry, Lua trampoline
```

---

## 18. Dependencies

| Library | Version | Purpose |
|---|---|---|
| libsodium | ≥ 1.0.20 | ChaCha20-Poly1305, X25519, HKDF-SHA256, HMAC-SHA256 |
| netc | HEAD (uzmigames/netc) | Adaptive per-connection packet compression |
| NanoSockets | HEAD | Cross-platform UDP socket with direct memory access |

All managed via vcpkg.

---

## 19. Related Specifications

| Spec | Scope |
|---|---|
| `net/live-content.md` | Content API server, in-memory DB pattern, CDN lazy-loading, admin in-game commands, quest hot-reload |
| `net/spell-system.md` | Data-driven spell/ability system, EffectDef, server-authoritative activation flow, Lua skill scripts |
| `net/navmesh-shared.md` | Auto-bake from collision geometry, `.uznavmesh` client/server parity, dynamic obstacles, CNavLink, NPC movement protocol |
| `net/creature-ai.md` | Server-authoritative behavior trees, boss phases, ability telegraphing (AbilityTelegraph RPC), aggro/threat, patrol, VSCode BT editor |
| `net/platform-services.md` | Central Platform API, account system (native+Steam+Google+Discord), Ed25519 server JWTs, game server registry, admin dashboard, CDN client, per-server isolation |
| `net/geodata.md` | Height-layer grid baked from Jolt geometry; XY-only `PlayerMoveTo` C2S (8 bytes); server Z resolution; multi-floor layers; 2D LoS walk; aerial mode; GEO_FLAG_DOOR dynamic toggle |
| `net/dynamic-blockers.md` | Runtime blocker layer: WALL (skill barriers), GATE (dungeon doors), REPULSION (force fields), TRAP (triggered areas); movement/LoS validation pipeline; NavMesh TileCache sync; siege barricades |

---

## 20. Definition of Done

- [ ] `UzBitStream` round-trip tests pass for all primitive types including `INT_MIN`, `INT_MAX`, `NaN`, `±Inf`, empty string, 64 KB string.
- [ ] ZigZag + VarUInt correct for `[−2³¹, 2³¹−1]`; VarUInt byte counts match table in §4.1.
- [ ] Quantisation error within 1 LSB for all encoding widths (4–32 bits).
- [ ] Quaternion round-trip angular error < 0.01°.
- [ ] Fragment reassembly correct for 1 B, 1 200 B, 65 535 B payloads; timeout discards partial groups.
- [ ] `RELIABLE_UNO` delivers 10 000 messages correctly with 10% simulated packet loss.
- [ ] `RELIABLE_ORD` delivers 1 000 messages in order with 30% out-of-order arrival.
- [ ] Replay window rejects all duplicates and all packets with sequence ≤ highest−64.
- [ ] ChaCha20-Poly1305 passes RFC 8439 AEAD test vectors; tampered packet is silently dropped.
- [ ] Stateless cookie verified; connection without valid cookie is rejected.
- [ ] X25519 handshake completes in under 5 ms LAN; session key identical on both sides.
- [ ] NAT traversal connects two clients behind full-cone NAT in under 3 s.
- [ ] Delta compression achieves ≥ 50% size reduction when 5% of fields change in a 60-field snapshot.
- [ ] netc integration achieves ≥ 0.45 compression ratio on typical 128-byte state-sync payload.
- [ ] Interest cell transition sends correct spawn/despawn RPCs with no double-send or missing entity.
- [ ] RPC macro expansion produces correct serialisation; Lua trampoline round-trips all types.
- [ ] `uz_server` sustains 1 000 simulated connections at 20 Hz with < 1 core CPU.
- [ ] Plugin ABI: game DLL loads, ticks, and unloads without crash or memory leak under ASan.
- [ ] All unit tests pass. Coverage ≥ 95%.

---

*See also: `live-content.md`, `spell-system.md`*
