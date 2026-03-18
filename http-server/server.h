#ifndef SERVER_H
#define SERVER_H

class Server {
public:
    explicit Server(int port);
    void start() const;

private:
    int port_;
};

void handle_client(int client_socket);

#endif