// mock_device_agent.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_PATH "/tmp/device_agent.sock"

int main() {
    int server_sock, client_sock;
    struct sockaddr_un addr;
    char buffer[256];

    unlink(SOCKET_PATH); // Remove existing socket file

    server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        exit(1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    listen(server_sock, 5);
    printf("Mock Device Agent listening on %s\n", SOCKET_PATH);

    while (1) {
        client_sock = accept(server_sock, NULL, NULL);
        if (client_sock < 0) continue;

        int len = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (len > 0) {
            buffer[len] = '\0';
            printf("Received metrics: %s\n", buffer);
            send(client_sock, "ACK", 3, 0);
        }

        close(client_sock);
    }

    close(server_sock);
    unlink(SOCKET_PATH);
    return 0;
}
