# Multi-threaded HTTP Server (C++)

Simple HTTP server built with C++ and POSIX sockets on Linux.

## Structure

```
http-server/
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

- `main.cpp`: starts the server
- `server.cpp`: socket lifecycle (`socket` / `bind` / `listen` / `accept`) and threading
- `http.cpp`: request parsing and response generation
- `utils.cpp`: helper utilities (logging, file loading)

## Build

```bash
g++ -std=c++17 -pthread main.cpp server.cpp http.cpp utils.cpp -o http_server
```

## Run

```bash
./http_server
```

Server listens on port `8080`.

## Quick test

```bash
curl -i http://127.0.0.1:8080/
curl -i http://127.0.0.1:8080/unknown
```

Expected:
- `/` → `200 OK` with body `Hello from C++ HTTP Server!`
- any other path → `404 Not Found`