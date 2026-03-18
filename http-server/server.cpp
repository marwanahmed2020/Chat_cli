#include "server.h"

#include "http.h"
#include "utils.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

Server::Server(int port) : port_(port) {}

void Server::start() const {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        throw std::runtime_error("Failed to create socket");
    }

    int reuse = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        close(server_socket);
        throw std::runtime_error("Failed to set SO_REUSEADDR");
    }

    sockaddr_in server_address {};
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port_);

    if (bind(server_socket, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) < 0) {
        close(server_socket);
        throw std::runtime_error("Bind failed");
    }

    if (listen(server_socket, 10) < 0) {
        close(server_socket);
        throw std::runtime_error("Listen failed");
    }

    std::cout << "HTTP server listening on port " << port_ << std::endl;

    while (true) {
        sockaddr_in client_info {};
        socklen_t client_len = sizeof(client_info);
        int client_socket = accept(server_socket, reinterpret_cast<sockaddr*>(&client_info), &client_len);

        if (client_socket < 0) {
            std::cerr << "Accept failed" << std::endl;
            continue;
        }

        std::thread client_thread(handle_client, client_socket);
        client_thread.detach();
    }
}

void handle_client(int client_socket) {
    constexpr size_t kBufferSize = 4096;
    char buffer[kBufferSize];

    int bytes = recv(client_socket, buffer, kBufferSize - 1, 0);
    if (bytes <= 0) {
        close(client_socket);
        return;
    }

    buffer[bytes] = '\0';
    std::string request(buffer);
    log_request(request);

    std::string response = build_http_response(request);
    send(client_socket, response.c_str(), response.size(), 0);

    close(client_socket);
}