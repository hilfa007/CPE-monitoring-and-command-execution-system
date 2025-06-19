#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>

#define CLOUD_HOST "127.0.0.1"
#define METRIC_PORT 8080
#define ALARM_PORT 8082
#define CLIENT_PORT 8083
#define MAX_MESSAGE_SIZE 2048
#define MAX_CLIENTS 10
#define METRIC_LOG_FILE "cloud_metrics.log"
#define ALARM_LOG_FILE "cloud_alarms.log"
#define ACCEPT_TIMEOUT 5

// Function Prototypes
void log_message(const char *message, const char *log_file);
void cleanup();
void signal_handler(int sig);
int create_server_socket(int port);
void handle_http_request(int client_fd, char *buffer, ssize_t len);
void broadcast_to_clients(const char *message, int exclude_fd);

// Global Variables
int metric_server_fd = -1;
int alarm_server_fd = -1;
int client_server_fd = -1;
int client_fds[MAX_CLIENTS] = {-1}; // Array of connected CLI client sockets
int num_clients = 0;

// Signal handler
void signal_handler(int sig) {
    fprintf(stderr, "Caught signal %d, cleaning up\n", sig);
    cleanup();
    exit(1);
}

// Log message to file
void log_message(const char *message, const char *log_file) {
    if (!message) {
        fprintf(stderr, "Error: NULL message\n");
        return;
    }
    time_t now = time(NULL);
    char *time_str = ctime(&now);
    if (!time_str) {
        fprintf(stderr, "Failed to get timestamp\n");
        return;
    }
    time_str[strcspn(time_str, "\n")] = '\0';

    FILE *log = fopen(log_file, "a");
    if (log) {
        fprintf(log, "%s,%s\n", time_str, message);
        fclose(log);
        fprintf(stderr, "Logged to %s: %s\n", log_file, message);
    } else {
        perror("Failed to open log file");
    }
}

// Cleanup resources
void cleanup() {
    if (metric_server_fd >= 0) {
        close(metric_server_fd);
        fprintf(stderr, "Closed metric server socket\n");
    }
    if (alarm_server_fd >= 0) {
        close(alarm_server_fd);
        fprintf(stderr, "Closed alarm server socket\n");
    }
    if (client_server_fd >= 0) {
        close(client_server_fd);
        fprintf(stderr, "Closed client server socket\n");
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] >= 0) {
            close(client_fds[i]);
            fprintf(stderr, "Closed client socket %d\n", client_fds[i]);
        }
    }
}

// Create TCP server socket
int create_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "Failed to create socket for port %d: %s\n", port, strerror(errno));
        return -1;
    }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        fprintf(stderr, "Failed to set SO_REUSEADDR for port %d: %s\n", port, strerror(errno));
        close(fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Failed to bind socket to port %d: %s\n", port, strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, 10) < 0) {
        fprintf(stderr, "Failed to listen on port %d: %s\n", port, strerror(errno));
        close(fd);
        return -1;
    }

    struct timeval tv = { .tv_sec = ACCEPT_TIMEOUT, .tv_usec = 0 };
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        fprintf(stderr, "Failed to set accept timeout for port %d: %s\n", port, strerror(errno));
        close(fd);
        return -1;
    }

    int bufsize = 65536;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize)) < 0) {
        fprintf(stderr, "Failed to set socket buffer size for port %d: %s\n", port, strerror(errno));
        close(fd);
        return -1;
    }

    fprintf(stderr, "Socket buffer size set to %d for port %d\n", bufsize, port);
    return fd;
}

// Handle HTTP request for alarms
void handle_http_request(int client_fd, char *buffer, ssize_t len) {
    if (strncmp(buffer, "POST /alarm", 11) != 0) {
        fprintf(stderr, "Invalid HTTP request: not POST /alarm\n");
        const char *response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, response, strlen(response), 0);
        return;
    }

    char *body = strstr(buffer, "\r\n\r\n");
    if (!body) {
        fprintf(stderr, "Invalid HTTP request: no body\n");
        const char *response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, response, strlen(response), 0);
        return;
    }
    body += 4;

    log_message(body, ALARM_LOG_FILE);
    broadcast_to_clients(body, client_fd);

    const char *response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    send(client_fd, response, strlen(response), 0);
}

// Broadcast message to all connected CLI clients
void broadcast_to_clients(const char *message, int exclude_fd) {
    char formatted_msg[MAX_MESSAGE_SIZE];
    snprintf(formatted_msg, sizeof(formatted_msg), "%s\n", message);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] >= 0 && client_fds[i] != exclude_fd) {
            ssize_t sent = send(client_fds[i], formatted_msg, strlen(formatted_msg), 0);
            if (sent < 0) {
                fprintf(stderr, "Failed to send to client %d: %s\n", client_fds[i], strerror(errno));
                close(client_fds[i]);
                client_fds[i] = -1;
                num_clients--;
            }
        }
    }
}

int main() {
    signal(SIGSEGV, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    // Initialize client_fds array
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_fds[i] = -1;
    }

    // Create metric server
    metric_server_fd = create_server_socket(METRIC_PORT);
    if (metric_server_fd < 0) {
        fprintf(stderr, "ERROR: Failed to start metric server on port %d\n", METRIC_PORT);
        cleanup();
        exit(1);
    }
    printf("Metric server running on %s:%d\n", CLOUD_HOST, METRIC_PORT);

    // Create alarm server
    alarm_server_fd = create_server_socket(ALARM_PORT);
    if (alarm_server_fd < 0) {
        fprintf(stderr, "ERROR: Failed to start alarm server on port %d\n", ALARM_PORT);
        cleanup();
        exit(1);
    }
    printf("Alarm server running on %s:%d\n", CLOUD_HOST, ALARM_PORT);

    // Create client server
    client_server_fd = create_server_socket(CLIENT_PORT);
    if (client_server_fd < 0) {
        fprintf(stderr, "ERROR: Failed to start client server on port %d\n", CLIENT_PORT);
        cleanup();
        exit(1);
    }
    printf("Client server running on %s:%d\n", CLOUD_HOST, CLIENT_PORT);

    // Poll for all sockets
    struct pollfd fds[3 + MAX_CLIENTS];
    fds[0].fd = metric_server_fd;
    fds[0].events = POLLIN;
    fds[1].fd = alarm_server_fd;
    fds[1].events = POLLIN;
    fds[2].fd = client_server_fd;
    fds[2].events = POLLIN;

    printf("Cloud Manager server running, listening for metrics, alarms, and clients\n");

    while (1) {
        // Update poll fds for clients
        int nfds = 3;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fds[i] >= 0) {
                fds[nfds].fd = client_fds[i];
                fds[nfds].events = POLLIN;
                nfds++;
            }
        }

        int ret = poll(fds, nfds, -1);
        if (ret < 0) {
            perror("Poll failed");
            continue;
        }

        // Metric server
        if (fds[0].revents & POLLIN) {
            int client_fd = accept(metric_server_fd, NULL, NULL);
            if (client_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    fprintf(stderr, "Accept timeout on metric server\n");
                    continue;
                }
                fprintf(stderr, "Failed to accept on metric server: %s\n", strerror(errno));
                continue;
            }
            fprintf(stderr, "Accepted connection on metric server\n");

            char buffer[MAX_MESSAGE_SIZE];
            ssize_t len = recv(client_fd, buffer, MAX_MESSAGE_SIZE - 1, 0);
            if (len <= 0) {
                if (len == 0) {
                    fprintf(stderr, "Client closed connection on metric server\n");
                } else {
                    fprintf(stderr, "Failed to receive on metric server: %s\n", strerror(errno));
                }
                close(client_fd);
                continue;
            }
            buffer[len] = '\0';

            if (strnlen(buffer, MAX_MESSAGE_SIZE) == 0 || strchr(buffer, '=') == NULL) {
                fprintf(stderr, "Invalid metric received, discarding\n");
                close(client_fd);
                continue;
            }
            fprintf(stderr, "Received metric: %s\n", buffer);
            log_message(buffer, METRIC_LOG_FILE);
            broadcast_to_clients(buffer, client_fd);
            close(client_fd);
            fprintf(stderr, "Closed connection on metric server\n");
        }

        // Alarm server
        if (fds[1].revents & POLLIN) {
            int client_fd = accept(alarm_server_fd, NULL, NULL);
            if (client_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    fprintf(stderr, "Accept timeout on alarm server\n");
                    continue;
                }
                fprintf(stderr, "Failed to accept on alarm server: %s\n", strerror(errno));
                continue;
            }
            fprintf(stderr, "Accepted connection on alarm server\n");

            char buffer[MAX_MESSAGE_SIZE];
            ssize_t len = recv(client_fd, buffer, MAX_MESSAGE_SIZE - 1, 0);
            if (len <= 0) {
                if (len == 0) {
                    fprintf(stderr, "Client closed connection on alarm server\n");
                } else {
                    fprintf(stderr, "Failed to receive on alarm server: %s\n", strerror(errno));
                }
                close(client_fd);
                continue;
            }
            buffer[len] = '\0';
            handle_http_request(client_fd, buffer, len);
            close(client_fd);
            fprintf(stderr, "Closed connection on alarm server\n");
        }

        // Client server
        if (fds[2].revents & POLLIN) {
            int client_fd = accept(client_server_fd, NULL, NULL);
            if (client_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    fprintf(stderr, "Accept timeout on client server\n");
                    continue;
                }
                fprintf(stderr, "Failed to accept on client server: %s\n", strerror(errno));
                continue;
            }

            if (num_clients >= MAX_CLIENTS) {
                fprintf(stderr, "Max clients reached, rejecting new client\n");
                close(client_fd);
                continue;
            }

            // Add new client
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_fds[i] < 0) {
                    client_fds[i] = client_fd;
                    num_clients++;
                    fprintf(stderr, "Accepted new CLI client (fd %d), total clients: %d\n", client_fd, num_clients);
                    break;
                }
            }
        }

        // Check existing clients
        for (int i = 3; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                char buffer[MAX_MESSAGE_SIZE];
                ssize_t len = recv(fds[i].fd, buffer, MAX_MESSAGE_SIZE - 1, 0);
                if (len <= 0) {
                    if (len == 0) {
                        fprintf(stderr, "CLI client %d disconnected\n", fds[i].fd);
                    } else {
                        fprintf(stderr, "Failed to receive from client %d: %s\n", fds[i].fd, strerror(errno));
                    }
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (client_fds[j] == fds[i].fd) {
                            close(client_fds[j]);
                            client_fds[j] = -1;
                            num_clients--;
                            fprintf(stderr, "Removed client, total clients: %d\n", num_clients);
                            break;
                        }
                    }
                    continue;
                }
                buffer[len] = '\0';
                fprintf(stderr, "Received from CLI client %d: %s\n", fds[i].fd, buffer);
                // Handle client commands if needed (e.g., request historical metrics)
            }
        }
    }

    cleanup();
    return 0;
}