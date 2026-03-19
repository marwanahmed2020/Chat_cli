#include "server.h"

#include "database.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <algorithm>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace {
struct RoomClient {
    int socket;
    std::string username;
};

std::unordered_map<std::string, std::vector<RoomClient>> room_clients;
std::mutex room_clients_mutex;
std::mutex log_mutex;

void server_log(const std::string& event) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&t, &tm_buf);
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cout << "[" << std::put_time(&tm_buf, "%H:%M:%S") << "] " << event << "\n";
}

Database& get_database() {
    static Database database("chat.db");
    static std::once_flag init_flag;
    static std::string initialization_error;

    std::call_once(init_flag, [&]() {
        if (!database.initialize(initialization_error)) {
            throw std::runtime_error("Database initialization failed: " + initialization_error);
        }
    });

    return database;
}

bool send_text(int client_socket, const std::string& message) {
    size_t total_sent = 0;
    while (total_sent < message.size()) {
        ssize_t sent = send(client_socket, message.data() + total_sent, message.size() - total_sent, 0);
        if (sent <= 0) {
            return false;
        }
        total_sent += static_cast<size_t>(sent);
    }
    return true;
}

bool receive_line(int client_socket, std::string& output) {
    output.clear();
    char ch = '\0';

    while (output.size() < 512) {
        ssize_t bytes = recv(client_socket, &ch, 1, 0);
        if (bytes <= 0) {
            return false;
        }

        if (ch == '\n') {
            break;
        }

        if (ch != '\r') {
            output.push_back(ch);
        }
    }

    while (!output.empty() && std::isspace(static_cast<unsigned char>(output.back()))) {
        output.pop_back();
    }

    return true;
}

bool prompt_line(int client_socket, const std::string& prompt, std::string& answer) {
    if (!send_text(client_socket, prompt)) {
        return false;
    }
    return receive_line(client_socket, answer);
}

void add_room_client(const std::string& room_code, int client_socket, const std::string& username) {
    std::lock_guard<std::mutex> lock(room_clients_mutex);
    room_clients[room_code].push_back({client_socket, username});
}

void remove_room_client(const std::string& room_code, int client_socket) {
    std::lock_guard<std::mutex> lock(room_clients_mutex);

    auto room_it = room_clients.find(room_code);
    if (room_it == room_clients.end()) {
        return;
    }

    auto& clients = room_it->second;
    clients.erase(std::remove_if(clients.begin(), clients.end(), [client_socket](const RoomClient& room_client) {
                      return room_client.socket == client_socket;
                  }),
                  clients.end());

    if (clients.empty()) {
        room_clients.erase(room_it);
    }
}

void broadcast_to_room(const std::string& room_code, const std::string& message, int exclude_socket = -1) {
    std::vector<int> targets;
    {
        std::lock_guard<std::mutex> lock(room_clients_mutex);
        auto room_it = room_clients.find(room_code);
        if (room_it == room_clients.end()) {
            return;
        }

        for (const auto& room_client : room_it->second) {
            if (room_client.socket != exclude_socket) {
                targets.push_back(room_client.socket);
            }
        }
    }

    for (int target_socket : targets) {
        send_text(target_socket, message);
    }
}

void run_room_chat_session(int client_socket, const std::string& username, const std::string& room_code) {
    add_room_client(room_code, client_socket, username);
    server_log("ROOM " + username + " entered room " + room_code);

    send_text(client_socket,
              "\nEntered room " + room_code + ".\n"
              "Type messages and press Enter. Type /leave to go back to room menu.\n\n");

    broadcast_to_room(room_code, "[system] " + username + " joined room " + room_code + "\n", client_socket);

    while (true) {
        std::string line;
        if (!receive_line(client_socket, line)) {
            server_log("DISCONN " + username + " disconnected from room " + room_code);
            break;
        }

        if (line == "/leave") {
            send_text(client_socket, "Leaving room " + room_code + "...\n\n");
            server_log("LEAVE " + username + " left room " + room_code);
            break;
        }

        if (line.empty()) {
            continue;
        }

        std::string formatted = "[" + room_code + "] " + username + ": " + line + "\n";
        server_log("MSG  [" + room_code + "] " + username + ": " + line);
        broadcast_to_room(room_code, formatted, client_socket);
        send_text(client_socket, "[you] " + line + "\n");
    }

    remove_room_client(room_code, client_socket);
    broadcast_to_room(room_code, "[system] " + username + " left room " + room_code + "\n", client_socket);
}
}  // namespace

Server::Server(int start_port, int end_port) : start_port_(start_port), end_port_(end_port) {}

void Server::start() {
    get_database();

    std::vector<std::thread> listeners;
    for (int port = start_port_; port <= end_port_; ++port) {
        listeners.emplace_back(&Server::run_listener, this, port);
    }

    for (auto& listener : listeners) {
        listener.join();
    }
}

void Server::start_placeholder_remove() {
}

void Server::run_admin_console() const {
    Database& database = get_database();
    std::string cmd;
    while (std::getline(std::cin, cmd)) {
        // trim trailing whitespace
        while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == ' ')) cmd.pop_back();

        if (cmd == "help") {
            std::cout << "\nAdmin commands:\n"
                      << "  rooms        - live room list with occupants\n"
                      << "  db users     - all registered users\n"
                      << "  db rooms     - all created rooms\n"
                      << "  db members   - all room memberships\n"
                      << "  help         - show this help\n\n";
        } else if (cmd == "rooms") {
            std::lock_guard<std::mutex> lock(room_clients_mutex);
            if (room_clients.empty()) {
                std::cout << "(no active rooms)\n";
            } else {
                std::cout << "\n--- Active Rooms ---\n";
                for (const auto& [code, clients] : room_clients) {
                    std::cout << "Room " << code << " (" << clients.size() << " member(s)):";
                    for (const auto& c : clients) std::cout << " " << c.username;
                    std::cout << "\n";
                }
                std::cout << "--------------------\n";
            }
        } else if (cmd == "db users") {
            database.admin_print_users(std::cout);
        } else if (cmd == "db rooms") {
            database.admin_print_rooms(std::cout);
        } else if (cmd == "db members") {
            database.admin_print_members(std::cout);
        } else if (!cmd.empty()) {
            std::cout << "Unknown command '" << cmd << "'. Type 'help'.\n";
        }
    }
    // stdin closed — sleep forever keeping listener threads alive
    for (;;) std::this_thread::sleep_for(std::chrono::hours(24));
}

void Server::run_listener(int port) const {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        std::cerr << "Failed to create socket for port " << port << std::endl;
        return;
    }

    int reuse = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        close(server_socket);
        std::cerr << "Failed to set SO_REUSEADDR for port " << port << std::endl;
        return;
    }

    sockaddr_in server_address {};
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port);

    if (bind(server_socket, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) < 0) {
        close(server_socket);
        std::cerr << "Bind failed on port " << port << std::endl;
        return;
    }

    if (listen(server_socket, 10) < 0) {
        close(server_socket);
        std::cerr << "Listen failed on port " << port << std::endl;
        return;
    }

    std::cout << "TCP auth/room server listening on port " << port << std::endl;

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
    if (!send_text(client_socket,
                   "Welcome!\n"
                   "Use a terminal client (telnet/nc) and send choices with Enter.\n\n")) {
        close(client_socket);
        return;
    }
    server_log("CONNECT fd=" + std::to_string(client_socket));

    Database& database = get_database();
    int user_id = -1;
    std::string username;

    while (user_id < 0) {
        if (!send_text(client_socket,
                       "Auth Menu:\n"
                       "1) Register\n"
                       "2) Login\n"
                       "3) Quit\n")) {
            close(client_socket);
            return;
        }

        std::string choice;
        if (!prompt_line(client_socket, "Choice: ", choice)) {
            close(client_socket);
            return;
        }

        if (choice == "1") {
            std::string password;
            std::string error;

            if (!prompt_line(client_socket, "New username: ", username) ||
                !prompt_line(client_socket, "New password (min 8 chars): ", password)) {
                close(client_socket);
                return;
            }

            if (database.create_user(username, password, error)) {
                send_text(client_socket, "Account created successfully. Please login.\n\n");
                server_log("REGISTER OK  username=" + username);
            } else {
                send_text(client_socket, "Register failed: " + error + "\n\n");
                server_log("REGISTER FAIL username=" + username + " reason=" + error);
            }
        } else if (choice == "2") {
            std::string password;
            std::string error;

            if (!prompt_line(client_socket, "Username: ", username) ||
                !prompt_line(client_socket, "Password: ", password)) {
                close(client_socket);
                return;
            }

            if (database.verify_user(username, password, user_id, error)) {
                send_text(client_socket, "Login successful.\n\n");
                server_log("LOGIN OK  username=" + username);
            } else {
                send_text(client_socket, "Login failed: " + error + "\n\n");
                server_log("LOGIN FAIL username=" + username + " reason=" + error);
            }
        } else if (choice == "3") {
            send_text(client_socket, "Goodbye.\n");
            close(client_socket);
            return;
        } else {
            send_text(client_socket, "Invalid choice.\n\n");
        }
    }

    while (true) {
        if (!send_text(client_socket,
                       "Room Menu:\n"
                       "1) Create room\n"
                       "2) Join room\n"
                       "3) Quit\n")) {
            close(client_socket);
            return;
        }

        std::string choice;
        if (!prompt_line(client_socket, "Choice: ", choice)) {
            close(client_socket);
            return;
        }

        if (choice == "1") {
            std::string error;
            std::string room_code = database.create_room(user_id, error);

            if (room_code.empty()) {
                send_text(client_socket, "Create room failed: " + error + "\n\n");
            } else {
                send_text(client_socket, "Room created. Share this room code: " + room_code + "\n");
                run_room_chat_session(client_socket, username, room_code);
            }
        } else if (choice == "2") {
            std::string room_code;
            std::string error;

            if (!prompt_line(client_socket, "Room code: ", room_code)) {
                close(client_socket);
                return;
            }

            if (database.join_room(user_id, room_code, error)) {
                send_text(client_socket, "Joined room " + room_code + " successfully.\n");
                run_room_chat_session(client_socket, username, room_code);
            } else {
                send_text(client_socket, "Join failed: " + error + "\n\n");
            }
        } else if (choice == "3") {
            send_text(client_socket, "Session ended.\n");
            close(client_socket);
            return;
        } else {
            send_text(client_socket, "Invalid choice.\n\n");
        }
    }
}