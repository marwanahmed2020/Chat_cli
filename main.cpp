#include "server.h"

#include <iostream>
#include <stdexcept>

// Usage: ./http_server              → port 8080
// Usage: ./http_server 9000         → port 9000
// Usage: ./http_server 8080 8082    → ports 8080, 8081, 8082
int main(int argc, char* argv[]) {
    int start_port = 8080;
    int end_port   = 8080;

    try {
        if (argc == 2) {
            start_port = end_port = std::stoi(argv[1]);
        } else if (argc >= 3) {
            start_port = std::stoi(argv[1]);
            end_port   = std::stoi(argv[2]);
        }
    } catch (...) {
        std::cerr << "Usage: ./http_server [start_port] [end_port]\n";
        return 1;
    }

    if (start_port > end_port) std::swap(start_port, end_port);

    std::cout << "Starting server on port";
    if (start_port == end_port)
        std::cout << " " << start_port;
    else
        std::cout << "s " << start_port << "-" << end_port;
    std::cout << " ... (type 'help' for admin commands)\n";

    Server server(start_port, end_port);
    server.start();
    return 0;
}