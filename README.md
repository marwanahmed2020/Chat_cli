# Terminal Chat Application

A simple terminal-based chat application built with C++ and sockets.

## Features
- Real-time messaging between multiple clients
- Server-client architecture
- Cross-platform compatibility (Linux)
- ZeroTier support for remote connections

## Project Structure
Terminal-Chat-App/
├── src/ # Source code
│ ├── server/ # Server implementation
├── include/ # Header files
├── docs/ # Documentation
├── tests/ # Unit tests
├── build/ # compiled binaries go here
└── scripts/ # Build scripts


## Building
```bash
# Build server
g++ -o build/server src/server/main.cpp

# Start server
./build/server

# Connect client
./build/client 127.0.0.1 8080

