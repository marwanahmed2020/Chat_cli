# Multi-threaded Auth + Room Server (C++)

Terminal-based concurrent TCP server built with C++ and POSIX sockets on Linux.

## Structure

```
http-server/
├── database.cpp
├── database.h
├── main.cpp
├── server.cpp
├── server.h
├── http.cpp
├── http.h
├── utils.cpp
├── utils.h
└── README.md
```

## Responsibilities

- `main.cpp`: starts the server over a port range
- `server.cpp`: multi-port listener loops, threading, and terminal menus
- `database.cpp`: SQLite schema + register/login + room create/join
- `http.cpp`, `utils.cpp`: existing helpers from previous stage

## Security choices

- Parameterized SQLite statements to reduce SQL injection risk
- Passwords are **hashed**, not plaintext, using PBKDF2-HMAC-SHA256
- Per-user cryptographic random salt (`RAND_bytes`)
- Constant-time hash comparison (`CRYPTO_memcmp`)

## Build

```bash
g++ -std=c++17 -pthread main.cpp server.cpp database.cpp http.cpp utils.cpp -lsqlite3 -lcrypto -o http_server
```

## Run

```bash
./http_server
```

At startup, provide a start/end port (for example `8080` to `8083`).
The server creates a listener loop on every port in that range.

## Terminal test

```bash
telnet 127.0.0.1 8080
# or
nc 127.0.0.1 8080
```

Flow:
- Register with username/password
- Login
- Create room (receives generated 6-digit code) OR join an existing room code