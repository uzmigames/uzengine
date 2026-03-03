# Platform Services Specification

**Status**: Draft
**Phase**: 4
**Scope**: Central API server, admin dashboard, multi-game-server topology, account system (native + Steam + Google + Discord), CDN asset delivery, and client service orchestration for MMORPG deployment.

---

## 1. System Overview

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              PLATFORM LAYER                                     │
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────┐                   │
│  │                  Platform API  (HTTP/2 + msgpack)        │                   │
│  │                                                          │                   │
│  │  /auth    /accounts    /servers    /content    /admin    │                   │
│  │                                                          │                   │
│  │  ┌─────────────────────────────────────────────────┐    │                   │
│  │  │  Admin Dashboard  (Web UI — same process)       │    │                   │
│  │  │  Items · Monsters · Spawns · Servers · Players  │    │                   │
│  │  └─────────────────────────────────────────────────┘    │                   │
│  └──────────────────────────────────────────────────────────┘                   │
│           │                   │                   │                              │
│     ┌─────▼──────┐     ┌──────▼──────┐     ┌─────▼──────┐                      │
│     │  Postgres  │     │   Redis     │     │    CDN     │                       │
│     │  Accounts  │     │  Sessions   │     │  Icons     │                       │
│     │  Audit Log │     │  ServerList │     │  Patches   │                       │
│     └────────────┘     └─────────────┘     └────────────┘                      │
└─────────────────────────────────────────────────────────────────────────────────┘
         │ Account Token + Server JWT                │ Assets
         ▼                                           ▼
┌──────────────────┐   ┌──────────────────┐   ┌───────────┐
│   Game Server A  │   │   Game Server B  │   │  Client   │
│   (Bartz realm)  │   │   (Giran realm)  │   │           │
│                  │   │                  │   │  Platform │
│  Players A       │   │  Players B       │   │  API  ────┼─ auth, serverlist
│  Items A         │   │  Items B         │   │           │
│  Economy A       │   │  Economy B       │   │  CDN  ────┼─ icons, patches
│                  │   │                  │   │           │
│  Content API ────┼───┼──────────────────┼─→ │  Game ────┼─ UDP game protocol
│  (per-server DB) │   │  (per-server DB) │   │  Server   │
└──────────────────┘   └──────────────────┘   └───────────┘
```

### 1.1 Core Principles

1. **Account is global, character is server-local.** One Platform account can have characters on multiple game servers. Items and economy are fully isolated per server (no cross-server trading). Classic L2/WoW realm model.
2. **Platform API is the single source of truth for identity.** Game servers trust Platform-signed JWTs; they never store passwords or OAuth tokens.
3. **Content API is per-server.** Each game server runs its own Content API instance backed by its own database (items, monsters, spawns specific to that server's configuration/rates).
4. **Dashboard is the frontend of the Platform API.** Same process, no separate deployment.
5. **CDN delivers assets** (icons, navmesh patches, event textures). 3D game assets ship via Steam/launcher delta patches — not CDN.

---

## 2. Service Topology

### 2.1 Platform API (Central)

Single central service. Responsible for:
- Account registration, login, password reset
- OAuth2/OpenID authentication (Steam, Google, Discord)
- Issuing session tokens and server-scoped JWTs
- Server registry (list of available game servers, capacity, status)
- Global ban list
- Admin dashboard UI + API

**Tech stack**: Go (single binary), Postgres (accounts/audit), Redis (sessions/serverlist cache).

### 2.2 Game Server (per realm)

Each realm (e.g., "Bartz", "Giran") is an independent deployment:
- Runs `uz_game_server` (C, UzEngine game.dll)
- Has its own Postgres instance (characters, inventory, economy, guilds)
- Has its own Content API instance (items, monsters, rates — customizable per server)
- Connects to Platform API on startup to register and on each login to validate tokens
- Players on server A cannot interact with players on server B

### 2.3 CDN

Static asset delivery via standard object storage + CDN (e.g., Cloudflare R2 or AWS S3 + CloudFront):
- Icon atlases (PNG, 4096×4096)
- NavMesh patches (`.uznavmesh`, per version hash)
- Event/seasonal textures and patches

---

## 3. Account System

### 3.1 Account Model

```sql
-- Platform Postgres

CREATE TABLE accounts (
    account_id      UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    email           VARCHAR(254) UNIQUE,        -- null for Steam-only accounts
    password_hash   CHAR(60),                   -- bcrypt cost=12; null for OAuth-only
    display_name    VARCHAR(32) NOT NULL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    last_login_at   TIMESTAMPTZ,
    status          SMALLINT NOT NULL DEFAULT 0, -- 0=active 1=suspended 2=banned
    ban_reason      TEXT,
    ban_expires_at  TIMESTAMPTZ                  -- null = permanent
);

CREATE TABLE oauth_identities (
    account_id  UUID REFERENCES accounts(account_id),
    provider    VARCHAR(16) NOT NULL,  -- 'steam' 'google' 'discord'
    subject     VARCHAR(128) NOT NULL, -- provider's user ID
    linked_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (provider, subject)
);

CREATE TABLE sessions (
    session_id      UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    account_id      UUID REFERENCES accounts(account_id),
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    expires_at      TIMESTAMPTZ NOT NULL,
    ip_address      INET,
    user_agent      TEXT,
    revoked         BOOLEAN NOT NULL DEFAULT false
);
-- Sessions also cached in Redis with TTL for fast validation

CREATE TABLE audit_log (
    log_id      BIGSERIAL PRIMARY KEY,
    account_id  UUID,
    action      VARCHAR(64) NOT NULL,  -- 'LOGIN' 'BAN' 'ITEM_GRANT' etc.
    detail      JSONB,
    ip_address  INET,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
```

### 3.2 Auth Providers

| Provider | Protocol | Flow |
|---|---|---|
| Native (email/password) | — | POST /auth/login → bcrypt verify → session |
| Steam | OpenID 2.0 | Redirect to Steam → validate signature → link/login |
| Google | OAuth2 + OIDC | Redirect → code exchange → id_token → link/login |
| Discord | OAuth2 | Redirect → code exchange → user endpoint → link/login |

One account can link multiple providers. Linking a new provider requires being already authenticated.

### 3.3 Token Architecture

```
Client ──[POST /auth/login]──► Platform API
                                    │
                                    ├── validate credentials
                                    ├── create session (Postgres + Redis)
                                    └── return:
                                        {
                                          session_token: "<UUID>",   // stored as HttpOnly cookie or in client keystore
                                          account_id:    "<UUID>",
                                          display_name:  "André",
                                          server_list:   [...]        // inline to save a round-trip
                                        }

Client ──[POST /auth/server-token]──► Platform API
        Body: { server_id: "bartz" }      │
                                          ├── validate session_token
                                          ├── check ban list
                                          └── return:
                                              {
                                                server_jwt: "<JWT signed with PLATFORM_PRIVATE_KEY>"
                                              }
                                              JWT payload:
                                              {
                                                sub:         "<account_id>",
                                                display_name: "André",
                                                server_id:   "bartz",
                                                iat:         <unix ts>,
                                                exp:         <iat + 300>  // 5 min; enough for handshake
                                              }

Client ──[UDP handshake + server_jwt]──► Game Server (Bartz)
                                              │
                                              ├── verify JWT signature (PLATFORM_PUBLIC_KEY, embedded at build)
                                              ├── check exp
                                              ├── look up or create Character for account_id on this server
                                              └── accept connection
```

**Key properties:**
- Game server never calls Platform API during login — it only verifies the JWT signature offline.
- Platform private key is HSM-protected (or env secret). Public key is baked into game server binary at build time.
- Server JWTs expire in 5 minutes — client requests a fresh one just before connecting.
- Session tokens are server-side (`sessions` table + Redis) — can be revoked immediately on ban.

---

## 4. Platform API Endpoints

### 4.1 Auth

```
POST /auth/register
     Body: { email, password, display_name }
     → { account_id, session_token }

POST /auth/login
     Body: { email, password }
     → { session_token, account_id, display_name, server_list }

POST /auth/logout
     Auth: session_token
     → 204

GET  /auth/steam/begin
     → redirect to Steam OpenID endpoint

GET  /auth/steam/callback?...
     → validate OpenID response → session → redirect to client deep link

GET  /auth/google/begin
     → redirect to Google OAuth2 authorization URL

GET  /auth/google/callback?code=...
     → exchange code → id_token → session → redirect

GET  /auth/discord/begin
     → redirect to Discord OAuth2 authorization URL

GET  /auth/discord/callback?code=...
     → exchange code → user endpoint → session → redirect

POST /auth/link/{provider}
     Auth: session_token
     Body: { oauth_code } or { openid_params }
     → 200 OK — link provider to existing account

POST /auth/server-token
     Auth: session_token
     Body: { server_id }
     → { server_jwt }   // 5 min TTL, server-scoped

POST /auth/password-reset/request
     Body: { email }
     → 204 (email sent if found)

POST /auth/password-reset/confirm
     Body: { token, new_password }
     → 204
```

### 4.2 Account

```
GET  /accounts/me
     Auth: session_token
     → { account_id, display_name, email, linked_providers, created_at }

PATCH /accounts/me
     Auth: session_token
     Body: { display_name?, email?, password? }
     → { account }

GET  /accounts/me/characters
     Auth: session_token
     → [ { server_id, character_name, level, class, last_login_at }, ... ]
     // Aggregated from all registered game servers
```

### 4.3 Server List

```
GET /servers
    Auth: session_token (or public — configurable)
    → [
        {
          server_id:    "bartz",
          display_name: "Bartz",
          region:       "us-east",
          status:       "online",        // online | maintenance | full
          population:   2341,            // current player count
          capacity:     3000,
          rates:        { exp: 1, drop: 1, adena: 1 },
          pvp:          true,
          new_player_friendly: false
        },
        ...
      ]
```

Game servers POST to `/internal/servers/heartbeat` every 10 s with current `population`. Platform API caches in Redis; TTL 30 s (server offline detection).

### 4.4 Admin API (requires `admin` role)

```
GET    /admin/accounts?q=&page=&limit=
GET    /admin/accounts/{id}
PATCH  /admin/accounts/{id}   Body: { status, ban_reason, ban_expires_at }
GET    /admin/accounts/{id}/audit

GET    /admin/servers
GET    /admin/servers/{id}/status   // relayed from game server health endpoint
POST   /admin/servers/{id}/broadcast  Body: { message }  // server-wide announce
POST   /admin/servers/{id}/shutdown   Body: { countdown_s }

GET    /admin/audit?account_id=&action=&from=&to=
```

---

## 5. Admin Dashboard

### 5.1 Overview

The Dashboard is a server-rendered + HTMX web UI served by the same Platform API binary under `/dashboard`. No separate frontend deployment; no JavaScript framework dependency.

```
Platform API binary
├── /api/...         — JSON/msgpack API endpoints
├── /auth/...        — Auth flows + OAuth callbacks
└── /dashboard/...   — HTML dashboard (template-rendered, HTMX for partial updates)
```

### 5.2 Dashboard Sections

```
Sidebar:
├── Overview         — server list, population graphs, active player count
├── Accounts         — search, view, ban/unban, audit log
├── Content          — items, monsters, spawns, skills (via Content API of selected server)
├── Servers          — per-server status, online players, zone health, admin commands
├── Audit Log        — searchable audit timeline
└── Settings         — Platform API config (OAuth credentials, capacity limits, ban defaults)
```

### 5.3 Content Management (via Content API)

The dashboard connects to each game server's Content API to manage game content. A server selector dropdown at the top lets admins switch between servers:

```
[Server: Bartz ▼] → all Content API calls go to bartz.game.yourdomain.com/v1/...
```

From the dashboard, admins can:
- CRUD items, monsters, skills, zones without editing JSON files directly
- Adjust spawn counts and respawn timers live
- Hot-reload quest Lua scripts
- Preview item icons (fetched from CDN)

Changes are applied via the Content API (same endpoints used by the game server), so all changes are live without restart.

### 5.4 In-Game Admin Commands

Admins with elevated session tokens can also use in-game console commands (from `live-content.md`):

```
/spawn create monster field_spider_elite x y z
/item grant player_name item_id count
/announce "Server restarting in 10 minutes"
/server kick player_name reason
/zone reload zone_id
```

These POST to the game server's admin IPC endpoint, authenticated with a short-lived admin JWT from the Platform API.

---

## 6. Game Server Registration and Communication

### 6.1 Server Registration (startup)

When a game server starts, it registers itself with the Platform API:

```
POST /internal/servers/register
Auth: INTERNAL_API_KEY  (shared secret, env var)
Body: {
  server_id:    "bartz",
  display_name: "Bartz",
  region:       "us-east",
  host:         "bartz.game.yourdomain.com",
  port:         7777,
  capacity:     3000,
  rates:        { exp: 1, drop: 1, adena: 1 },
  pvp:          true
}
→ { registered: true, platform_public_key: "<PEM>" }
```

The Platform API returns the current public key so the game server can verify future tokens. Key rotation updates are delivered via the heartbeat response.

### 6.2 Heartbeat

```
POST /internal/servers/heartbeat
Auth: INTERNAL_API_KEY
Body: {
  server_id:   "bartz",
  population:  2341,
  zone_health: [ { zone_id: "talking_island", players: 312, tick_ms: 8 }, ... ]
}
→ {
    ban_list_delta: [ { account_id, action: "ban"|"unban" }, ... ],  // incremental bans since last heartbeat
    motd: "Welcome to Bartz!",
    platform_public_key: null  // non-null only on key rotation
  }
```

Game servers process `ban_list_delta` immediately — active sessions for banned accounts are terminated (ForceDisconnect RPC to client).

### 6.3 Player Login Flow (end-to-end)

```
1. Client → Platform API:  POST /auth/login  →  { session_token }
2. Client → Platform API:  GET /servers       →  server list (pick "Bartz")
3. Client → Platform API:  POST /auth/server-token { server_id: "bartz" }  →  { server_jwt }
4. Client → Game Server:   UDP handshake + server_jwt
5. Game Server:            verify JWT (offline, public key)
                           look up Character for account_id in local DB
                           if not found: create new character (first login on this server)
                           send LoginSuccess, zone assignment, initial snapshot
```

No Platform API call on step 4–5. Game server is fully autonomous after token verification.

---

## 7. Client Service Orchestration

The game client interacts with three independent services:

```
┌────────────────────────────────────────────────────────────┐
│                        GAME CLIENT                         │
│                                                            │
│  ┌─────────────────┐  ┌──────────────────┐  ┌──────────┐  │
│  │  Platform API   │  │   CDN            │  │  Game    │  │
│  │  Client         │  │   Client         │  │  Server  │  │
│  │                 │  │                  │  │  Client  │  │
│  │  · login        │  │  · icon atlas    │  │          │  │
│  │  · server list  │  │    lazy-load     │  │  · UDP   │  │
│  │  · server JWT   │  │  · navmesh patch │  │    game  │  │
│  │  · account info │  │  · event assets  │  │    loop  │  │
│  │  · password     │  │                  │  │          │  │
│  │    reset        │  │  Background,     │  │  · RPCs  │  │
│  │                 │  │  non-blocking    │  │  · chat  │  │
│  └────────┬────────┘  └──────────────────┘  └────┬─────┘  │
│           │                                       │        │
│           └───────────── Lua game.dll API ────────┘        │
│              ctx.platform.login(email, pass, cb)           │
│              ctx.platform.server_list(cb)                  │
│              ctx.cdn.icon(item_id, cb)                     │
└────────────────────────────────────────────────────────────┘
```

### 7.1 Client Lua API

```lua
-- Authentication
ctx.platform.login(email, password, function(err, result)
    if err then show_error(err) return end
    -- result.display_name, result.session_token (stored in keystore)
end)

ctx.platform.login_steam(function(err, result)
    -- Steam overlay opens; callback fires on completion
end)

ctx.platform.login_google(function(err, result)
    -- Opens system browser; deep link returns token
end)

ctx.platform.login_discord(function(err, result) ... end)

ctx.platform.logout()

-- Server selection
ctx.platform.server_list(function(err, servers)
    -- servers[i].server_id, .display_name, .population, .status, .rates
end)

ctx.platform.connect(server_id, function(err)
    -- Internally: get server_jwt → UDP handshake
end)

-- CDN (async, cached)
ctx.cdn.icon(item_id, function(texture_handle)
    -- Returns immediately with cached handle, or loads async
end)

ctx.cdn.icon_batch(item_ids, function(handles_map) ... end)
```

### 7.2 Session Persistence

The client stores `session_token` in the OS credential store (Windows Credential Manager on Windows). On next launch, it validates the stored token with `GET /accounts/me` — if valid, skips the login screen.

---

## 8. Content API — Per-Server Isolation

Each game server runs its own Content API instance. Items, monsters, and rates are per-server:

```
Bartz game server:
  content-api-bartz.yourdomain.com
  DB: bartz_content (Postgres)
  Rates: exp×1, drop×1 (classic)
  Custom monsters: disabled

Giran game server:
  content-api-giran.yourdomain.com
  DB: giran_content (Postgres)
  Rates: exp×5, drop×3 (x5 server)
  Custom monsters: extra event monsters enabled
```

The Platform API does not manage game content — it only manages accounts, sessions, and server registry. Content changes per server are independent.

---

## 9. Security Considerations

### 9.1 Trust Boundaries

| Boundary | Mechanism |
|---|---|
| Client → Platform API | HTTPS, session token (HttpOnly cookie or client keystore) |
| Client → Game Server | UDP + ChaCha20-Poly1305 + server_jwt in handshake |
| Game Server → Platform API | HTTPS + INTERNAL_API_KEY (env var, not in binary) |
| Dashboard → Platform API | Same session system, `admin` role check per endpoint |
| Game Server token verification | Ed25519 public key baked into game server binary at build |

### 9.2 Rate Limiting

All Platform API endpoints have rate limits enforced at the reverse proxy (Nginx / Caddy):

| Endpoint | Limit |
|---|---|
| POST /auth/login | 5 req/min per IP |
| POST /auth/register | 3 req/min per IP |
| POST /auth/server-token | 10 req/min per session |
| GET /servers | 60 req/min per session |
| Admin endpoints | 120 req/min per admin session |

### 9.3 Account Security

- Passwords: bcrypt cost=12, min 8 chars, breach-check via HaveIBeenPwned API on registration
- Sessions: 30-day TTL, sliding expiry on activity
- Concurrent session limit: 3 (oldest revoked on exceed)
- Ban propagation: game server bans synchronous via heartbeat delta (max 10 s lag)

---

## 10. Deployment Model

```
Production:
  platform-api.yourdomain.com     — Platform API + Dashboard (single Go binary)
  content-api-{server}.yourdomain.com  — one per game server
  cdn.yourdomain.com              — Cloudflare R2 or S3+CloudFront

  Postgres (platform):  managed DB (Supabase / RDS / self-hosted)
  Postgres (per-server): self-hosted per game server machine
  Redis:                managed (Upstash / ElastiCache / self-hosted)

Development:
  All services run locally via docker-compose
  Platform API: localhost:8080
  Game Server A: localhost:7777 (UDP)
  Content API A: localhost:9090
  Postgres: localhost:5432
  Redis: localhost:6379
```

### 10.1 docker-compose (development)

```yaml
services:
  platform-api:
    image: uzengine/platform-api
    environment:
      DATABASE_URL: postgres://uz:uz@postgres:5432/platform
      REDIS_URL: redis://redis:6379
      PLATFORM_PRIVATE_KEY_PATH: /secrets/platform.key
      STEAM_API_KEY: ${STEAM_API_KEY}
      GOOGLE_CLIENT_ID: ${GOOGLE_CLIENT_ID}
      GOOGLE_CLIENT_SECRET: ${GOOGLE_CLIENT_SECRET}
      DISCORD_CLIENT_ID: ${DISCORD_CLIENT_ID}
      DISCORD_CLIENT_SECRET: ${DISCORD_CLIENT_SECRET}
      INTERNAL_API_KEY: ${INTERNAL_API_KEY}
    ports: ["8080:8080"]

  game-server-bartz:
    image: uzengine/game-server
    environment:
      SERVER_ID: bartz
      CONTENT_API_URL: http://content-api-bartz:9090
      PLATFORM_API_URL: http://platform-api:8080
      INTERNAL_API_KEY: ${INTERNAL_API_KEY}
      DATABASE_URL: postgres://uz:uz@postgres:5432/bartz
    ports: ["7777:7777/udp"]

  content-api-bartz:
    image: uzengine/content-api
    environment:
      DATABASE_URL: postgres://uz:uz@postgres:5432/bartz_content
    ports: ["9090:9090"]

  postgres:
    image: postgres:16
    ...

  redis:
    image: redis:7
    ...
```

---

## 11. File Layout

```
src/platform/
├── main.go                     — entrypoint, HTTP server, router
├── config/
│   └── config.go               — env-based config (DATABASE_URL, keys, etc.)
├── auth/
│   ├── handler.go              — /auth endpoints
│   ├── session.go              — session create/validate/revoke (Postgres + Redis)
│   ├── jwt.go                  — server_jwt issue + verify (Ed25519)
│   ├── bcrypt.go               — password hash/verify
│   ├── steam.go                — Steam OpenID 2.0 flow
│   ├── google.go               — Google OAuth2 + OIDC flow
│   └── discord.go              — Discord OAuth2 flow
├── accounts/
│   ├── handler.go              — /accounts endpoints
│   └── model.go                — Account, OAuthIdentity DB models
├── servers/
│   ├── handler.go              — /servers, /internal/servers/*
│   ├── registry.go             — server registration + heartbeat, Redis cache
│   └── ban_propagation.go      — ban_list_delta assembly
├── admin/
│   ├── handler.go              — /admin/* endpoints (role check middleware)
│   └── audit.go                — audit log query
├── dashboard/
│   ├── router.go               — /dashboard/* routes
│   ├── templates/              — HTML templates (Go html/template)
│   │   ├── layout.html
│   │   ├── overview.html
│   │   ├── accounts.html
│   │   ├── content.html
│   │   ├── servers.html
│   │   └── audit.html
│   └── static/                 — CSS, minimal JS (HTMX)
└── middleware/
    ├── auth.go                 — session validation middleware
    ├── rate_limit.go           — per-IP + per-session rate limiting
    └── cors.go                 — CORS for client API calls

src/engine/net/platform/
├── uz_platform_client.h/.c     — C client: login, server_list, server_jwt, session persistence
├── uz_cdn_client.h/.c          — C client: icon atlas lazy-load, navmesh download
└── uz_platform_lua.h/.c        — Lua ctx.platform.* and ctx.cdn.* bindings

tests/platform/
├── test_auth_native.c          — register, login, session validate, revoke
├── test_auth_steam.c           — Steam OpenID flow mock
├── test_auth_google.c          — Google OAuth2 flow mock
├── test_server_jwt.c           — JWT issue, verify, expiry, wrong key rejection
├── test_server_registry.c      — register, heartbeat, population update, offline detection
├── test_ban_propagation.c      — ban → heartbeat delta → game server disconnect
├── test_rate_limit.c           — /auth/login rate limit enforcement
└── test_platform_client.c      — C client login flow, server JWT request, CDN fetch
```

---

## 12. Dependencies

| Library | Language | Purpose |
|---|---|---|
| Go standard library | Go | HTTP/2 server, crypto/ed25519, crypto/bcrypt |
| `pgx/v5` | Go | Postgres driver |
| `go-redis/v9` | Go | Redis client |
| `golang.org/x/oauth2` | Go | Google + Discord OAuth2 flows |
| `yohcop/openid-go` | Go | Steam OpenID 2.0 verification |
| `html/template` | Go | Dashboard server-side rendering |
| HTMX | JS (CDN) | Dashboard partial page updates, no JS build step |
| libcurl | C | Platform API + CDN HTTP client (game engine side) |
| cJSON | C | JSON parse for API responses (game engine side) |

---

## 13. Definition of Done

### Account System
- [ ] Native register/login with bcrypt cost=12; password breach-check on registration.
- [ ] Steam OpenID 2.0 flow: redirect → validate → link or create account → session.
- [ ] Google OAuth2 + OIDC flow: redirect → code exchange → id_token → link or create → session.
- [ ] Discord OAuth2 flow: redirect → code exchange → user endpoint → link or create → session.
- [ ] Multiple providers linkable to one account; unlinking last provider requires native password set.
- [ ] Session stored in Postgres + Redis; `GET /accounts/me` validates in < 5 ms (Redis hit).
- [ ] Session revocation (logout, ban) propagates to Redis immediately.
- [ ] Concurrent session limit: oldest session revoked when limit exceeded.

### Server JWT
- [ ] `POST /auth/server-token` issues Ed25519-signed JWT with 5-min TTL, server_id claim.
- [ ] Game server verifies JWT offline (no Platform API call on login).
- [ ] JWT with wrong server_id rejected by game server.
- [ ] Expired JWT rejected by game server.
- [ ] Key rotation: new public key delivered via heartbeat response; old JWTs grace period 60 s.

### Server Registry
- [ ] Game server registers on startup; appears in `/servers` response.
- [ ] Heartbeat every 10 s; population shown live in dashboard and server list.
- [ ] Server marked `offline` if heartbeat missed for 30 s; removed from client server list.
- [ ] Ban list delta propagated in heartbeat; game server disconnects banned player within 10 s.

### Admin Dashboard
- [ ] `/dashboard` accessible to `admin`-role accounts; all other accounts get 403.
- [ ] Account search, view, ban/unban with reason and expiry, audit trail.
- [ ] Server overview shows all registered servers, population, zone health.
- [ ] Content management (items, monsters, spawns) via Content API of selected server.
- [ ] Audit log searchable by account, action, date range.

### Client Integration
- [ ] `uz_platform_client` C API: login (native + OAuth), server list, server JWT, session persist/restore.
- [ ] Lua `ctx.platform.login`, `ctx.platform.login_steam`, `ctx.platform.server_list`, `ctx.platform.connect` all work end-to-end.
- [ ] CDN client: icon atlas fetch, navmesh patch download, background non-blocking.
- [ ] On session restore (app relaunch): skips login screen if stored session still valid.

### Security
- [ ] Rate limits enforced: 5 req/min login, 3 req/min register.
- [ ] INTERNAL_API_KEY never logged or returned in API responses.
- [ ] Platform private key not baked into binary — loaded from env/secret at startup.
- [ ] All `/admin/*` endpoints require `admin` role; non-admin sessions get 403.

### Tests
- [ ] All tests pass. Coverage ≥ 95%.
