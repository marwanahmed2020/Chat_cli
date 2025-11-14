#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <thread>
#include <cstdlib>
#include <ctime>

using namespace std;




// i want to create send and recv functions to make it easier with the resusablilty
void send_message(int socket,string message)
{
    send(socket, message.c_str(), message.length(), 0);
}


void recv_message(int socket)
{
    char buffer[1024];
    // Initially: [ ][ ][ ][ ][ ][ ]


    int bytes_received = recv(socket, buffer, sizeof(buffer)-1, 0);
    // After receiving "Hello" (5 bytes):
    // buffer: ['H']['e']['l']['l']['o'][ ]
    // bytes_received = 5    
            
    if (bytes_received > 0)
    {
        buffer[bytes_received] = '\0'; //buffer: ['H']['e']['l']['l']['o']['\0']
        cout << "Client said: " << buffer;
            
        // 🆕 my turn to response!
        cout << "💬 Type your response: ";
    }
            

}

int randomBetween(int min, int max) {
    if (min==max)
    { 
        return max;
    }
    return rand() % (max - min + 1) + min;
}


int main()
{

    srand(time(0));
    
    cout << "=== Continuous Chat Server ===" << endl;
    cout << "Server will run forever until you press Ctrl+C" << endl;
    // Setup server (ONE TIME)
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(12345);  // ← CHANGED TO 12345
    server_address.sin_addr.s_addr = INADDR_ANY;

    // Try to bind - if fails, try another port 
    // i will use the function randomBetween to get random ports if one if them is busy i will try until i find one that is not busy
    htons(12345);  
    if (bind(server_socket, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
        cout << "❌ Port 12345 busy! Trying 23456..." << endl;
        server_address.sin_port = htons(23456);  // Try alternative port
        if (bind(server_socket, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
            cout << "❌ Bind failed! Try yet another port." << endl;
            close(server_socket);
            return 1;
        }
    }
    listen(server_socket, 5);

    cout << "✅ Server ready! Friends can connect anytime." << endl;
    cout << "Local: telnet 127.0.0.1 12345" << endl;
    cout << "ZeroTier: telnet 10.242.150.235 12345" << endl;

    
    cout << "\n🔄 Waiting for new client..." << endl;
        
    while(true){

    // Wait for ANY client to connect (local or ZeroTier)
    struct sockaddr_in client_info;
    socklen_t client_len = sizeof(client_info);
    int client_socket = accept(server_socket, (struct sockaddr*)&client_info, &client_len);
        
    cout << "🎉 New client connected from: " << inet_ntoa(client_info.sin_addr) << endl;

   

    // 🆕 MAIN LOOP - Keep server running forever

    while (true) {
       
        // this massage will apear on his terminal
        // const char* welcome = "Hello! Type a message:\n";
        // send(client_socket, welcome, strlen(welcome), 0);

        send_message(client_socket,"Hello! Type a message:\n");

        // Chat with this client
        thread recv_mssg(recv_message);
/*
        char buffer[100];
        int bytes_received = recv(client_socket, buffer, 99, 0);
        
        
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            cout << "Client said: " << buffer;
            
         // 🆕 YOUR TURN TO RESPONSE!
        cout << "💬 Type your response: ";
*/    
        string reply;
        getline(cin, reply);  // ← FIX: Use getline() instead of cin>>
        if(reply == "bye")
        {
            reply =reply+"\n";
            send(client_socket, reply.c_str(), reply.length(), 0);
            close(client_socket);
            cout << "Client disconnected." << endl;
            break; // loop break
        }
        else
        {
            reply =reply+"\n";
            send(client_socket, reply.c_str(), reply.length(), 0);
        }
        // Send your response
        cout << "Sent your response: " << reply << endl;
        }



        

        // Close THIS client connection but keep server running
       
        
        // 🆕 SERVER DOES NOT EXIT - goes back to accept() for next client
    }

    close(server_socket);
    return 0;

}

    // This line never reached because of infinite loop


/*

i want to open thread for reciving and sending also multiple
messages at the same time

1- i will enhance the operations of sending and reciving 
2- make the function more readable and reusable easily
to use in threads like send and recv.
3- adding threading for sending and reciving





*/