# C++ Multi-threaded TCP Chat Server

Concurrent terminal chat server in C++ with:

- TCP sockets (POSIX)
- Multi-threading (`std::thread`)
- SQLite persistence
- OpenSSL security primitives
- Live room chat with many clients per room
- Server admin console + event logging

Clients connect with `nc` or `telnet`, register/login, create or join a room, and chat in real time.

---

## Features

- User registration and login
- Password hashing with PBKDF2-HMAC-SHA256 + per-user random salt
- Constant-time hash compare (`CRYPTO_memcmp`)
- Room creation with cryptographically random 6-digit code
- Room membership persistence (`chat.db`)
- In-memory live room state for fast broadcast
- `/leave` to exit room without disconnecting
- Multi-port listening (single port or range)
- Admin console commands:
  - `help`
  - `rooms`
  - `db users`
  - `db rooms`
  - `db members`
- Server-side event logs (connect, register, login, join/leave, message, disconnect)

---

## Project Structure

```text
Chat_cli/
├── main.cpp
├── server.h
├── server.cpp
├── database.h
├── database.cpp
├── http.h
├── http.cpp
├── utils.h
├── utils.cpp
├── chat.db         # auto-created
├── http_server     # build output
└── README.md
```

---

## Architecture Overview

```text
main.cpp
  └─ Server(start_port, end_port).start()
       ├─ one detached listener thread per port
       │    └─ accept() loop
       │         └─ one detached thread per client -> handle_client(fd)
       └─ admin console thread (stdin commands)
```

```text
Client flow:
Connect -> Auth Menu (Register/Login/Quit) -> Room Menu (Create/Join/Quit)
      -> Live Chat
          - send message to room members
          - /leave returns to Room Menu
          - disconnect cleans room state and notifies others
```

---

## Security Model

- Passwords are never stored in plaintext.
- Registration:
  1. Generate 16-byte random salt with `RAND_bytes`
  2. Hash password via PBKDF2-HMAC-SHA256 (120000 iterations)
  3. Store `password_hash` + `salt` in SQLite
- Login:
  1. Fetch hash+salt for username
  2. Recompute PBKDF2
  3. Compare using `CRYPTO_memcmp`
- SQL statements use prepared queries + bind parameters.

---

## Database Schema

`chat.db` has 3 tables:

- `users(id, username, password_hash, salt, created_at)`
- `rooms(room_code, owner_user_id, created_at)`
- `room_members(room_code, user_id, joined_at)`

`room_members` has a composite primary key `(room_code, user_id)` so duplicates are prevented.

---

## Dependencies (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install -y g++ libsqlite3-dev libssl-dev netcat-openbsd
```

---

## Build

```bash
g++ -std=c++17 -pthread main.cpp server.cpp database.cpp http.cpp utils.cpp -lsqlite3 -lcrypto -o http_server
```

## Build + Run (single command)

```bash
g++ -std=c++17 -pthread main.cpp server.cpp database.cpp http.cpp utils.cpp -lsqlite3 -lcrypto -o http_server && ./http_server
```

---

## Run

Default port `8080`:

```bash
./http_server
```

Specific single port:

```bash
./http_server 9000
```

Port range:

```bash
./http_server 8080 8082
```

On startup, the server also accepts admin commands from the same terminal.

---

## Connect Clients

Client terminal:

```bash
nc 127.0.0.1 8080
```

Open multiple terminals for multiple clients (Client 1, Client 2, ...), all using the same command.

---

## Quick 2-Client Test

1. Terminal A: start server

```bash
./http_server 8080
```

2. Terminal B: connect first client

```bash
nc 127.0.0.1 8080
```

3. In Client 1:
   - Register
   - Login
   - Create room (get code, e.g. `482931`)

4. Terminal C: connect second client

```bash
nc 127.0.0.1 8080
```

5. In Client 2:
   - Register/Login
   - Join room using `482931`

6. Send messages from either side and verify broadcast.

---

## Admin Console Commands

Type in the **server terminal**:

```text
help
rooms
db users
db rooms
db members
```

### Command meanings

- `rooms`: active in-memory rooms, member count, usernames
- `db users`: all users from SQLite
- `db rooms`: all rooms from SQLite
- `db members`: all room memberships from SQLite

---

## Logging

Server logs events like:

- `CONNECT`
- `REGISTER OK/FAIL`
- `LOGIN OK/FAIL`
- `ROOM CREATE/JOIN OK/FAIL`
- `MSG`
- `LEAVE`
- `DISCONN`

Example:

```text
[16:43:23] CONNECT fd=5
[16:44:10] LOGIN OK  username=alice
[16:44:20] ROOM CREATE OK  username=alice code=482931
```

---

## Troubleshooting

### `./http_server: command not found` or `No such file`

Build first, then run:

```bash
g++ -std=c++17 -pthread main.cpp server.cpp database.cpp http.cpp utils.cpp -lsqlite3 -lcrypto -o http_server
./http_server
```

Check binary exists:

```bash
ls -l http_server
```

### `bash: syntax error near unexpected token '('`

You pasted Markdown links into terminal. Use plain filenames only.

### Client cannot connect

- Ensure server is running
- Ensure correct port
- Check listener:

```bash
ss -ltnp | grep 8080
```

### Port already in use

Run on another port:

```bash
./http_server 9090
```

---

## Notes

- `http.h/.cpp` and `utils.h/.cpp` are currently auxiliary from earlier stage; core chat runtime is in `server.*` + `database.*`.
- Many clients can join the same room and chat concurrently.
