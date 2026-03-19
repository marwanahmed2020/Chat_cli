#ifndef SERVER_H
#define SERVER_H

#include <vector>

class Server {
public:
    Server(int start_port, int end_port);
    void start();

private:
    int start_port_;
    int end_port_;

    void run_listener(int port) const;
    void run_admin_console() const;
};

void handle_client(int client_socket);

#endif