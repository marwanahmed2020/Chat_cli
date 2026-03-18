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
main thread
  └─ Server::start()
       ├─ thread → run_listener(8080)   accept() loop forever
       ├─ thread → run_listener(8081)   accept() loop forever
       └─ thread → run_listener(8082)   accept() loop forever
```

### 2 — Per-client threads

Every `accept()` call spawns a **detached** `std::thread(handle_client, socket_fd)`. That thread owns the socket for the client's entire session and cleans up when the client disconnects.

### 3 — Client session flow

```
Connect
  └─ Auth Menu
       ├─ 1) Register  → username + password (min 8 chars) → stored hashed
       ├─ 2) Login     → verified → Room Menu
       └─ 3) Quit

     Room Menu
       ├─ 1) Create room → server generates 6-digit code → enter chat
       ├─ 2) Join room   → enter code → enter chat
       └─ 3) Quit

          Live Chat
            ├─ type message → broadcast to everyone else in the room
            ├─ others see   [482931] alice: hello
            ├─ you see      [you] hello
            └─ /leave       → back to Room Menu
```

### 4 — Room broadcast

An in-memory `unordered_map<string, vector<RoomClient>>` tracks which sockets are currently in each room. A `std::mutex` protects this map. When a message arrives, the lock is taken just long enough to snapshot the target socket list, then released before any `send()` calls — so one slow client never blocks others.

If a client drops (broken pipe / Ctrl+C), `receive_line()` returns `false`, the chat loop exits, the socket is removed from the map, and a leave notification is broadcast to the remaining clients.

---

## Database Design

`chat.db` is created automatically on first run. Three tables:

```sql
users         (id, username, password_hash, salt, created_at)
rooms         (room_code PK, owner_user_id FK, created_at)
room_members  (room_code FK, user_id FK, joined_at)  ← composite PK
```

`PRAGMA foreign_keys = ON` is set per connection. All queries that touch user input use `sqlite3_prepare_v2` + `sqlite3_bind_*` — never string concatenation.

---

## Security

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

## CV Description

> **Multi-threaded Authenticated TCP Chat Server (C++)**
>
> - Built a concurrent TCP server in C++ using raw POSIX sockets on Linux, supporting simultaneous connections across a configurable port range
> - Implemented per-port listener threads and per-client detached threads for fully non-blocking concurrent handling
> - Designed a secure authentication system: passwords stored as PBKDF2-HMAC-SHA256 hashes with per-user cryptographic random salts (OpenSSL)
> - Applied constant-time hash comparison (`CRYPTO_memcmp`) to prevent timing-oracle attacks during login
> - Persisted user accounts and room memberships in SQLite using parameterized queries to prevent SQL injection
> - Implemented real-time room broadcast with mutex-protected in-memory session state, join/leave notifications, and graceful disconnect cleanup
> - Applied modular architecture separating networking, authentication, persistence, and protocol layers into dedicated translation units
