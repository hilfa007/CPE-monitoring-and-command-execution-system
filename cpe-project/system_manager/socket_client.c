#include "socket_client.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define SOCKET_PATH "/tmp/device_agent.sock"

int send_metrics_to_agent(Metrics m) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return 0;

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return 0;
    }

    char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "memory=%.2f,cpu=%.2f,uptime=%.2f,disk=%.2f,net=%d,proc=%d",
             m.memory, m.cpu, m.uptime, m.disk, m.net_interfaces, m.processes);

    send(sock, buffer, strlen(buffer), 0);

    char ack[16] = {0};
    int len = recv(sock, ack, sizeof(ack) - 1, 0);
    if (len > 0) {
        ack[len] = '\0';
        printf("Received ACK from Device Agent: %s\n", ack);  // <-- Print ACK here
    }

    close(sock);
    return (len > 0 && strncmp(ack, "ACK", 3) == 0);
}
