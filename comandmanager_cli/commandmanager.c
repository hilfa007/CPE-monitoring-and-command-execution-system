#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pty.h>
#include <utmp.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define LOG_FILE "commands.log"


// List of interactive commands that need longer timeout
const char *long_timeout_cmds[] = {"vim", "nano", "vi", "cat", "less", "more","man", NULL};

// Check if the command is interactive
int is_long_timeout_command(const char *cmd) {
    for (int i = 0; long_timeout_cmds[i] != NULL; i++) {
        if (strstr(cmd, long_timeout_cmds[i]) == cmd) {
            return 1;
        }
    }
    return 0;
}


void log_command(const char *command, const char *output) {
    FILE *log = fopen("commands.log", "a");
    if (log) {
        time_t now = time(NULL);
        char *timestamp = ctime(&now);
        if (timestamp) {
            timestamp[strcspn(timestamp, "\n")] = '\0'; // Remove newline
        }
        fprintf(log, "\n[%s] Command: %s\nOutput:\n%s\n", timestamp, command, output);
        fclose(log);
    } else {
        perror("Failed to open log file");
    }
}


void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    char command[BUFFER_SIZE];
    int bytes = read(client_socket, command, sizeof(command) - 1);
    if (bytes <= 0) {
        close(client_socket);
        return NULL;
    }
    command[bytes] = '\0';

    int timeout_seconds = is_long_timeout_command(command) ? 300 : 5;

    int master_fd;
    pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);
    if (pid < 0) {
        perror("forkpty failed");
        close(client_socket);
        return NULL;
    }

    if (pid == 0) {
        // Check if the command ends with '&'
        int len = strlen(command);
        int is_background = (len > 0 && command[len - 1] == '&');
    
        // Wrap the command to ensure background jobs are waited on
        char wrapped_command[BUFFER_SIZE * 2];
        if (is_background) {
            snprintf(wrapped_command, sizeof(wrapped_command), "%s wait", command);
        } else {
            snprintf(wrapped_command, sizeof(wrapped_command), "%s", command);
        }
    
        // Use bash for better job control
        execlp("bash", "bash", "-c", wrapped_command, (char *)NULL);
        perror("execlp failed");
        exit(1);
    }
    

    // Parent process
    fd_set fds;
    char buffer[BUFFER_SIZE];
    char output[BUFFER_SIZE * 10] = {0};
    int total_output = 0;

    time_t last_activity = time(NULL);
    struct timeval timeout;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(client_socket, &fds);
        FD_SET(master_fd, &fds);
        int maxfd = (client_socket > master_fd ? client_socket : master_fd) + 1;

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(maxfd, &fds, NULL, NULL, &timeout);
        time_t now = time(NULL);

        if (activity < 0) break;

        if (FD_ISSET(client_socket, &fds)) {
            int n = read(client_socket, buffer, sizeof(buffer));
            if (n <= 0) break;
            write(master_fd, buffer, n);
            last_activity = now;
        }

        if (FD_ISSET(master_fd, &fds)) {
            int n = read(master_fd, buffer, sizeof(buffer));
            if (n <= 0) break;
            write(client_socket, buffer, n);
            if (total_output + n < sizeof(output)) {
                memcpy(output + total_output, buffer, n);
                total_output += n;
            }
            last_activity = now;
        }

 
        if (difftime(now, last_activity) >= timeout_seconds) {
            printf("Session timed out after %d seconds of inactivity.\n", timeout_seconds);
            break;
        }
            
    }

    close(master_fd);
    close(client_socket);
    waitpid(pid, NULL, 0);

    output[total_output] = '\0';
    log_command(command, output);
    return NULL;
}

int main() {
    int server_fd;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }

    printf("Server listening on port %d...\n", PORT);

    while (1) {
        int *client_socket = malloc(sizeof(int));
        if (!client_socket) continue;

        *client_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (*client_socket < 0) {
            perror("Accept failed");
            free(client_socket);
            continue;
        }

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, client_socket);
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}
