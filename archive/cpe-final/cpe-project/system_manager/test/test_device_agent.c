#include <stdio.h>
#include <stdio.h> // For printf
#include <stdlib.h>
#include <stdlib.h> // For exit
#include <string.h>
#include <string.h> // For memset, strcpy
#include <unistd.h>
#include <unistd.h> // For unlink, close
#include <sys/socket.h>
#include <sys/socket.h> // For socket, bind, listen, accept, recv, send
#include <sys/un.h>
#include <sys/un.h> // For sockaddr_un

#define SOCKET_PATH "/tmp/device_agent.sock"
//#define SOCKET_PATH "/tmp/device_agent.sock" // Path for Unix socket

int main() {
    int server_sock, client_sock;
    //int server_sock, client_sock; // Server and client socket descriptors
    struct sockaddr_un addr;
    //struct sockaddr_un addr; // Unix socket address structure
    char buffer[256];
    //char buffer[256]; // Buffer for received data

    unlink(SOCKET_PATH);
    //unlink(SOCKET_PATH); // Remove existing socket file

    server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    //server_sock = socket(AF_UNIX, SOCK_STREAM, 0); // create Unix stream socket
    if (server_sock < 0) {
        perror("socket");
        exit(1);
    }

    memset(&addr, 0, sizeof(addr)); // Clear address structure
    addr.sun_family = AF_UNIX;
    //addr.sun_family = AF_UNIX; // Set address family
    strcpy(addr.sun_path, SOCKET_PATH);
    //strcpy(addr.sun_path, SOCKET_PATH); // Set socket path

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        //if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) // Bind socket to address
        perror("bind");
        exit(1);
    }

    listen(server_sock, 5);
    //listen(server_sock, 5); // Listen for connections
    printf("Mock Device Agent listening on %s\n", SOCKET_PATH);
    //printf("Mock Device Agent listening on %s\n", SOCKET_PATH); // Print listening status

    while (1) {
        client_sock = accept(server_sock, NULL, NULL);
        //client_sock = accept(server_sock, NULL, NULL); // Accept client connection
        if (client_sock < 0) continue;

        int len = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        //int len = recv(client_sock, buffer, sizeof(buffer) - 1, 0); // Receive data
        if (len > 0) {
            buffer[len] = '\0';
            //buffer[len] = '\0'; // Null-terminate data
            printf("Received metrics: %s\n", buffer);
            //printf("Received metrics: %s\n", buffer); // Print received data
            send(client_sock, "ACK", 3, 0);
            //send(client_sock, "ACK", 3, 0); // Send acknowledgment
        }

        close(client_sock); // Close client socket
    }

    close(server_sock); // Close server socket (unreachable)
    unlink(SOCKET_PATH); // Remove socket file (unreachable)
    return 0;
}