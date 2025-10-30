#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
using namespace std;

int main()
{
    cout << "Continuous Chat Server" << endl;
    cout << "Server will run forever until i press Ctrl+C or say bye" << endl;

    // im now creating the server socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(12345);  
    server_address.sin_addr.s_addr = INADDR_ANY;

    // this if condition for checking if the port 12345 bind or its busy 
    // becuase when i tried the first time to connect locally the client could not connect 
    // that is becuase the port was busy and i did not now where is the problem from
    // so i searched about the bind function and then discoverd that it should return -1 if it cant bind 
    if (bind(server_socket, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
        cout << "Port 12345 busy! Trying 23456..." << endl;
        server_address.sin_port = htons(23456);  // Trying alternative port
        if (bind(server_socket, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
            cout << "Bind failed! Try yet another port." << endl;
            close(server_socket);
            return 1;
        }
    }

    
    listen(server_socket, 5);

    cout << "Server ready!" << endl;
    cout << "Local: telnet 127.0.0.1 12345" << endl;
    cout << "ZeroTier: telnet 10.242.150.235 12345" << endl;

    
    cout << "\n Waiting for new client..." << endl;
        
    while(true){

    
    // waiting for any client to connect (locally or ZeroTier)
    struct sockaddr_in client_info;
    socklen_t client_len = sizeof(client_info);
    int client_socket = accept(server_socket, (struct sockaddr*)&client_info, &client_len);
    cout << "New client connected from: " << inet_ntoa(client_info.sin_addr) << endl;

    while (true) {
       
        // this massage will apear on his terminal
        const char* welcome = "Hello! Type a message:\n";
        send(client_socket, welcome, strlen(welcome), 0);

        // Chat with this client
        

        char buffer[100];
        int bytes_received = recv(client_socket, buffer, 99, 0);
        
        
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            cout << "Client said: " << buffer;
            
         // this massage will appear on my terminal when its my turn to send a massage
        cout << "Type your response: ";
    
        string reply;
        getline(cin, reply);  // i Used getline() instead of cin>> to fix incompaitable data types passing
        if(reply == "bye")
        {
            send(client_socket, reply.c_str(), reply.length(), 0);
            close(client_socket);
            cout << "Client disconnected." << endl;
            break; // loop break
        }
        else
        {
            send(client_socket, reply.c_str(), reply.length(), 0);
        }
        // Send my response
        cout << "Sent your response: " << reply << endl;
        }
        // Close THIS client connection but keep server running
        // SERVER DOES NOT EXIT - goes back to accept() for next client
        // i made the first while loop to keep the server running for other users
    }

}

    // This line never reached because of infinite loop
    close(server_socket);
    return 0;
}