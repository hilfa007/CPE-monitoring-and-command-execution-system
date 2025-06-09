#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

#define UNIX_SOCK_PATH "/tmp/device_agent.sock"
#define CLOUD_HOST "127.0.0.1"
#define CLOUD_PORT 8080
#define MAX_METRIC_SIZE 256
#define CONNECT_RETRIES 5
#define RETRY_DELAY 2

int cloud_server_fd = -1;

// Mock Cloud Manager server
void *start_cloud_manager(void *arg) {
    cloud_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (cloud_server_fd < 0) {
        perror("Failed to create Cloud Manager socket");
        exit(1);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(CLOUD_PORT);

    int opt = 1;
    setsockopt(cloud_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(cloud_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Failed to bind Cloud Manager socket");
        exit(1);
    }

    if (listen(cloud_server_fd, 10) < 0) {
        perror("Failed to listen on Cloud Manager socket");
        exit(1);
    }

    printf("Mock Cloud Manager running on %s:%d\n", CLOUD_HOST, CLOUD_PORT);

    int client_fd = accept(cloud_server_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("Failed to accept connection");
        exit(1);
    }

    char buffer[MAX_METRIC_SIZE];
    while (1) {
        ssize_t len = recv(client_fd, buffer, MAX_METRIC_SIZE - 1, 0);
        if (len <= 0) break;
        buffer[len] = '\0';
        printf("Cloud Manager received: %s\n", buffer);
    }

    close(client_fd);
    close(cloud_server_fd);
    return NULL;
}

// Simulate System Manager sending metrics with retries
void send_metric(const char *metric) {
    int retries = CONNECT_RETRIES;
    while (retries > 0) {
        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("Failed to create client socket");
            return;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, UNIX_SOCK_PATH, sizeof(addr.sun_path) - 1);

        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            fprintf(stderr, "Failed to connect to Device Agent: %s\n", strerror(errno));
            close(sock);
            retries--;
            if (retries > 0) {
                sleep(RETRY_DELAY);
                continue;
            }
            return;
        }

        if (send(sock, metric, strlen(metric), 0) < 0) {
            perror("Failed to send metric");
        } else {
            char ack[4];
            ssize_t len = recv(sock, ack, sizeof(ack) - 1, 0);
            if (len > 0) {
                ack[len] = '\0';
                printf("Received ACK: %s\n", ack);
            }
        }

        close(sock);
        return;
    }
}

int main() {
    // Start mock Cloud Manager in a separate process
    pid_t pid = fork();
    if (pid == 0) {
        start_cloud_manager(NULL);
        exit(0);
    }

    // Wait for Cloud Manager to start
    sleep(1);

    // Send initial metrics with Cloud Manager available
    printf("Sending metrics with Cloud Manager available...\n");
    send_metric("{\"memory\":30, \"cpu\":1}");
    sleep(1);
    send_metric("{\"memory\":32, \"cpu\":2}");
    sleep(1);

    // Stop Cloud Manager to simulate disconnection
    printf("Simulating Cloud Manager disconnection...\n");
    kill(pid, SIGTERM);
    wait(NULL);
    cloud_server_fd = -1;

    // Send metrics during disconnection (should be buffered)
    printf("Sending metrics during disconnection...\n");
    send_metric("{\"memory\":35, \"cpu\":3}");
    sleep(1);
    send_metric("{\"memory\":38, \"cpu\":4}");
    sleep(1);

    // Restart Cloud Manager to trigger buffer flush
    printf("Restarting Cloud Manager to flush buffer...\n");
    pid = fork();
    if (pid == 0) {
        start_cloud_manager(NULL);
        exit(0);
    }

    // Wait longer for Device Agent to flush buffer
    sleep(15);

    // Check if Device Agent is still running
    if (access(UNIX_SOCK_PATH, F_OK) < 0) {
        fprintf(stderr, "Device Agent socket missing, likely crashed\n");
        kill(pid, SIGTERM);
        wait(NULL);
        exit(1);
    }

    // Send final metric
    send_metric("{\"memory\":40, \"cpu\":5}");

    // Cleanup
    kill(pid, SIGTERM);
    wait(NULL);
    return 0;
}