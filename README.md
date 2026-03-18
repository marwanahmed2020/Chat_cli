# C++ Multi-threaded TCP Chat Server

A concurrent, terminal-based chat server built from scratch in C++ using raw POSIX sockets, POSIX threads (`std::thread`), SQLite for persistence, and OpenSSL for cryptographic security. Clients connect over TCP using `nc` or `telnet`, register/login, create or join chat rooms, and exchange messages in real time.

---

## What is `main.cpp`?

`main.cpp` is the **only entry point**. It asks for a port range at startup, then hands control to `Server`:

```cpp
Server server(start_port, end_port);
server.start();   // spawns one listener thread per port, runs forever
```

Everything else (socket lifecycle, auth menus, password hashing, DB access, room broadcast) is delegated to the modules below.

---

## Project Structure

```
Chat_cli/
├── main.cpp        entry point — reads port range, starts Server
├── server.h/cpp    networking, threading, terminal menus, room broadcast
├── database.h/cpp  SQLite schema, register/login, room create/join
├── http.h/cpp      HTTP request parser and response builder
├── utils.h/cpp     request logger and file reader helpers
├── http_server     compiled binary (after build)
├── chat.db         SQLite database (created on first run)
└── README.md       this file
```

---

## How the Server Works

### 1 — Multi-port listener loop

`Server::start()` spawns one `std::thread` per port in the given range. Each thread runs its own `socket → bind → listen → accept` loop independently.

```
┌─────────────────────────────────────────────────────────────────┐
│                         main thread                             │
│                       Server::start()                           │
└──────────┬──────────────────┬──────────────────┬───────────────┘
           │                  │                  │
           ▼                  ▼                  ▼
  ┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
  │ run_listener()  │ │ run_listener()  │ │ run_listener()  │
  │   port 8080     │ │   port 8081     │ │   port 8082     │
  │                 │ │                 │ │                 │
  │  socket()       │ │  socket()       │ │  socket()       │
  │  bind()         │ │  bind()         │ │  bind()         │
  │  listen()       │ │  listen()       │ │  listen()       │
  │  accept() ────► │ │  accept() ────► │ │  accept() ────► │
  │  (blocks)       │ │  (blocks)       │ │  (blocks)       │
  └─────────────────┘ └─────────────────┘ └─────────────────┘
```

### 2 — Per-client threads

Every `accept()` call spawns a **detached** `std::thread(handle_client, socket_fd)`. That thread owns the socket for the client's entire session and cleans up when the client disconnects.

```
  accept() on port 8080
        │
        ├──► client A connects ──► detached thread ──► handle_client(fd_A)
        │
        ├──► client B connects ──► detached thread ──► handle_client(fd_B)
        │
        └──► client C connects ──► detached thread ──► handle_client(fd_C)
                                                              │
                                                    (each thread lives
                                                     until disconnect,
                                                     then self-destructs)
```

### 3 — Client session flow

```
  Client connects (nc / telnet)
           │
           ▼
  ┌─────────────────────────────────┐
  │           AUTH MENU             │
  ├─────────────────────────────────┤
  │  1) Register                    │
  │  2) Login                       │◄─── loops until login succeeds
  │  3) Quit ──────────────────────►│ disconnect
  └──────────────┬──────────────────┘
                 │ login OK
                 ▼
  ┌─────────────────────────────────┐
  │           ROOM MENU             │
  ├─────────────────────────────────┤
  │  1) Create room                 │──► generates 6-digit code
  │  2) Join room ──► enter code    │
  │  3) Quit ──────────────────────►│ disconnect
  └──────────────┬──────────────────┘
                 │ entered a room
                 ▼
  ┌─────────────────────────────────────────────────────┐
  │                   LIVE CHAT                         │
  ├─────────────────────────────────────────────────────┤
  │                                                     │
  │  you type: "hello"                                  │
  │       │                                             │
  │       ├──► you see:    [you] hello                  │
  │       └──► others see: [482931] alice: hello        │
  │                                                     │
  │  others type ──► you see: [482931] bob: hey         │
  │                                                     │
  │  /leave ────────────────────────────────────────────┼──► back to Room Menu
  │  disconnect ────────────────────────────────────────┼──► cleanup + notify room
  └─────────────────────────────────────────────────────┘
```

### 4 — Room broadcast

An in-memory `unordered_map<string, vector<RoomClient>>` tracks which sockets are currently in each room. A `std::mutex` protects this map. When a message arrives, the lock is taken just long enough to snapshot the target socket list, then released before any `send()` calls — so one slow client never blocks others.

```
  room_clients map (protected by std::mutex)
  ┌──────────────────────────────────────────────┐
  │  "482931" ──► [ {fd_A, "alice"},              │
  │                 {fd_B, "bob"  },              │
  │                 {fd_C, "carol"} ]             │
  │  "771840" ──► [ {fd_D, "dave" } ]             │
  └──────────────────────────────────────────────┘

  alice sends "hello"
        │
        ▼
  lock mutex ──► copy [fd_B, fd_C] ──► unlock mutex
        │
        ├──► send(fd_B, "[482931] alice: hello")
        └──► send(fd_C, "[482931] alice: hello")
             (fd_A skipped — that's alice herself)

  alice disconnects abruptly
        │
        ▼
  receive_line() returns false
        │
        ├──► remove fd_A from room map
        └──► broadcast "[system] alice left room 482931" to fd_B, fd_C
```

If a client drops (broken pipe / Ctrl+C), `receive_line()` returns `false`, the chat loop exits, the socket is removed from the map, and a leave notification is broadcast to the remaining clients.

---

## Database Design

`chat.db` is created automatically on first run. Three tables:

```
  ┌──────────────────────────┐        ┌───────────────────────────┐
  │          users           │        │          rooms            │
  ├──────────────────────────┤        ├───────────────────────────┤
  │ id           INTEGER PK  │◄───┐   │ room_code   TEXT PK       │
  │ username     TEXT UNIQUE │    │   │ owner_user_id INTEGER FK ─┼──► users.id
  │ password_hash TEXT       │    │   │ created_at  DATETIME      │
  │ salt         TEXT        │    │   └─────────────┬─────────────┘
  │ created_at   DATETIME    │    │                 │
  └──────────────────────────┘    │                 │
                                   │   ┌─────────────▼─────────────┐
                                   │   │       room_members         │
                                   │   ├───────────────────────────┤
                                   │   │ room_code  TEXT FK         │
                                   └───┼─user_id    INTEGER FK      │
                                       │ joined_at  DATETIME        │
                                       │ PRIMARY KEY(room_code,     │
                                       │             user_id)       │
                                       └───────────────────────────┘
```

`PRAGMA foreign_keys = ON` is set per connection. All queries that touch user input use `sqlite3_prepare_v2` + `sqlite3_bind_*` — never string concatenation.

---

## Security

```
  Registration flow
  ─────────────────
  password (plaintext)
       │
       ▼
  RAND_bytes(16) ──► salt (hex)
       │
       ▼
  PKCS5_PBKDF2_HMAC(password, salt, 120000 iters, SHA-256)
       │
       ▼
  password_hash (hex) ──► stored in SQLite   (plaintext never saved)

  Login flow
  ──────────
  username ──► SELECT password_hash, salt FROM users
                              │
  submitted password ─────────┼──► re-run PBKDF2 with stored salt
                              │
                              ▼
                    CRYPTO_memcmp(stored_hash, computed_hash)
                              │
                    ┌─────────┴──────────┐
                   match               no match
                    │                    │
                 Login OK          "Invalid username or password"
                                   (same message — no username enumeration)
```

| Concern | Implementation |
|---------|---------------|
| Password storage | PBKDF2-HMAC-SHA256, 120 000 iterations, 32-byte output |
| Salt | 16 bytes from `RAND_bytes` (OpenSSL), unique per user |
| Login comparison | `CRYPTO_memcmp` — constant-time, prevents timing-oracle attacks |
| SQL injection | Parameterized statements everywhere |
| Room codes | Generated with `RAND_bytes`, not `rand()` |
| Oversized input | `receive_line()` caps each line at 512 bytes |
| Socket reuse | `SO_REUSEADDR` on every listener |

---

## Dependencies

```bash
sudo apt install libsqlite3-dev libssl-dev
```

| Library | Used for |
|---------|---------|
| `libsqlite3` | Embedded database |
| `libssl` / `libcrypto` | PBKDF2, RAND_bytes, CRYPTO_memcmp |
| POSIX sockets | TCP networking (built-in on Linux) |
| `<thread>`, `<mutex>` | C++17 standard library (built-in) |

---

## Build

```bash
g++ -std=c++17 -pthread \
    main.cpp server.cpp database.cpp http.cpp utils.cpp \
    -lsqlite3 -lcrypto \
    -o http_server
```

---

## Run

```bash
./http_server
# Start port: 8080
# End port:   8080
```

Enter the same number for both to listen on a single port. Enter a range (e.g. 8080–8083) to open multiple ports simultaneously.

---

## Quick Test

Open two terminals:

```bash
# Terminal 1 — Alice
nc 127.0.0.1 8080
# → Register alice / secret123  → Login → Create room → gets code e.g. 482931

# Terminal 2 — Bob
nc 127.0.0.1 8080
# → Register bob / hunter456   → Login → Join room → 482931
```

Alice's terminal shows `[system] bob joined room 482931`.  
Bob types `hello` → Alice sees `[482931] bob: hello`.  
Bob types `/leave` → Alice sees `[system] bob left room 482931`.

---



> **Multi-threaded Authenticated TCP Chat Server (C++)**
>
> - Built a concurrent TCP server in C++ using raw POSIX sockets on Linux, supporting simultaneous connections across a configurable port range
> - Implemented per-port listener threads and per-client detached threads for fully non-blocking concurrent handling
> - Designed a secure authentication system: passwords stored as PBKDF2-HMAC-SHA256 hashes with per-user cryptographic random salts (OpenSSL)
> - Applied constant-time hash comparison (`CRYPTO_memcmp`) to prevent timing-oracle attacks during login
> - Persisted user accounts and room memberships in SQLite using parameterized queries to prevent SQL injection
> - Implemented real-time room broadcast with mutex-protected in-memory session state, join/leave notifications, and graceful disconnect cleanup
> - Applied modular architecture separating networking, authentication, persistence, and protocol layers into dedicated translation units
