# Networking

UDP multiplayer stack — server architecture, replication, RPCs, and lag compensation.

> **Status**: Planned
> **Source**: `runtime/engine/network/`

| Document | Topic |
|----------|-------|
| [01-overview.md](01-overview.md) | Network stack overview, UDP transport, binary serialization |
| [02-server-architecture.md](02-server-architecture.md) | Dedicated server model, authority model, zone management |
| [03-replication.md](03-replication.md) | State replication, delta compression, interest management |
| [04-rpc-system.md](04-rpc-system.md) | RPC macros, reliable/unreliable channels, SACK |
| [05-lag-compensation.md](05-lag-compensation.md) | Lag compensation — hit detection rewind, client-side prediction |
