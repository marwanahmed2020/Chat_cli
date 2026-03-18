#include "server.h"

#include <iostream>
#include <utility>

int main() {
    int start_port = 8080;
    int end_port = 8080;

    std::cout << "Start port: ";
    std::cin >> start_port;
    std::cout << "End port: ";
    std::cin >> end_port;

    if (start_port > end_port) {
        std::swap(start_port, end_port);
    }

    Server server(start_port, end_port);
    server.start();
    return 0;
}