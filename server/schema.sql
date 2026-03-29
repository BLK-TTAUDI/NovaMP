-- NovaMP Master Server Schema
-- SQLite dialect (also valid PostgreSQL with minor type adjustments)

PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

-- Users
CREATE TABLE IF NOT EXISTS users (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT    NOT NULL UNIQUE COLLATE NOCASE,
    email         TEXT    NOT NULL UNIQUE COLLATE NOCASE,
    password_hash TEXT,
    discord_id    TEXT    UNIQUE,
    role          TEXT    NOT NULL DEFAULT 'player',
    banned        INTEGER NOT NULL DEFAULT 0,
    ban_reason    TEXT,
    created_at    TEXT    NOT NULL DEFAULT (datetime('now')),
    last_login    TEXT
);

-- Auth Tokens (JWT refresh tokens)
CREATE TABLE IF NOT EXISTS auth_tokens (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    token_hash TEXT    NOT NULL UNIQUE,
    expires_at TEXT    NOT NULL,
    created_at TEXT    NOT NULL DEFAULT (datetime('now'))
);

-- Registered Game Servers
CREATE TABLE IF NOT EXISTS game_servers (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    name             TEXT    NOT NULL,
    description      TEXT    NOT NULL DEFAULT '',
    host             TEXT    NOT NULL,
    port             INTEGER NOT NULL,
    map              TEXT    NOT NULL DEFAULT 'gridmap_v2',
    max_players      INTEGER NOT NULL DEFAULT 8,
    current_players  INTEGER NOT NULL DEFAULT 0,
    password_protected INTEGER NOT NULL DEFAULT 0,
    version          TEXT    NOT NULL DEFAULT '1.0.0',
    auth_token       TEXT    NOT NULL UNIQUE,
    owner_id         INTEGER REFERENCES users(id),
    tags             TEXT    NOT NULL DEFAULT '[]',
    last_heartbeat   TEXT    NOT NULL DEFAULT (datetime('now')),
    registered_at    TEXT    NOT NULL DEFAULT (datetime('now'))
);

-- Mods
CREATE TABLE IF NOT EXISTS mods (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT    NOT NULL,
    filename    TEXT    NOT NULL UNIQUE,
    version     TEXT    NOT NULL,
    size_bytes  INTEGER NOT NULL,
    sha256      TEXT    NOT NULL,
    description TEXT    NOT NULL DEFAULT '',
    uploader_id INTEGER REFERENCES users(id),
    downloads   INTEGER NOT NULL DEFAULT 0,
    uploaded_at TEXT    NOT NULL DEFAULT (datetime('now'))
);

-- Ban List
CREATE TABLE IF NOT EXISTS bans (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER REFERENCES users(id),
    ip_address TEXT,
    reason     TEXT    NOT NULL,
    expires_at TEXT,
    banned_by  INTEGER REFERENCES users(id),
    created_at TEXT    NOT NULL DEFAULT (datetime('now'))
);

-- Statistics
CREATE TABLE IF NOT EXISTS stats (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    total_players   INTEGER NOT NULL DEFAULT 0,
    peak_players    INTEGER NOT NULL DEFAULT 0,
    total_servers   INTEGER NOT NULL DEFAULT 0,
    recorded_at     TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_game_servers_heartbeat ON game_servers(last_heartbeat);
CREATE INDEX IF NOT EXISTS idx_auth_tokens_user ON auth_tokens(user_id);
CREATE INDEX IF NOT EXISTS idx_bans_user ON bans(user_id);
CREATE INDEX IF NOT EXISTS idx_bans_ip ON bans(ip_address);
