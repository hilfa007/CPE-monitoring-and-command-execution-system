// TCP connection to device agent (socket - client)

#include "device_agent_client.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

// the path for the Unix domain socket
#define SOCKET_PATH "/tmp/device_agent.sock"

// send system metrics to a device agent via Unix domain socket
int send_metrics_to_agent(Metrics m) {
    // Create a Unix domain socket
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("socket creation failed\n");
        return 0;
    }
         // Return 0 if socket creation fails

    // set up the socket address structure
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX; // specify Unix domain socket
    strcpy(addr.sun_path, SOCKET_PATH); // set socket path

    // connect to the device agent socket
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("connection to device agent failed\n");
        close(sock); // close socket on connection failure
        return 0; // return 0 if connection fails
    }

    // Buffer to store formatted metrics string
    char buffer[256];
    // Format metrics into a comma-separated string
    snprintf(buffer, sizeof(buffer),
             "memory=%.2f,cpu=%.2f,uptime=%.2f,disk=%.2f,net=%d,proc=%d",
             m.memory, m.cpu, m.uptime, m.disk, m.net_interfaces, m.processes);

    // send the formatted metrics string to the device agent
    if(send(sock, buffer, strlen(buffer), 0)) {
        printf("Metric send to device agent.\n");
    } else {
        printf("ERROR: Failed to send metrics.\n");
    }

    // Buffer to store acknowledgment response
    char ack[16] = {0};
    // Receive acknowledgment from the device agent
    int len = recv(sock, ack, sizeof(ack) - 1, 0);
    if (len > 0) {
        ack[len] = '\0'; // Null-terminate the received string
        // print the received acknowledgment
        printf("Received ACK from Device Agent: %s\n", ack);
    }

    // Close the socket
    close(sock);
    // return 1 if valid ACK received, 0 otherwise
    return (len > 0 && strncmp(ack, "ACK", 3) == 0);
}
