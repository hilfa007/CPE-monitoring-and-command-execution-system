#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/file.h>

#define BUFFER_SIZE 1024
#define METRIC_PORT 8080 // For Device Agent metrics
#define COMMAND_PORT 8081 // For Command Manager
#define MAX_METRIC_SIZE 256
#define DEFAULT_TIMEOUT 5
#define LONG_TIMEOUT 300
#define LOG_FILE "cloud_metrics.log"

// List of interactive commands requiring longer timeout
const char *long_timeout_cmds[] = {"vim", "nano", "vi", "cat", "less", "more", "man", NULL};

// Global for cloud server socket
int cloud_server_fd = -1;

// Signal handler for cleanup
void signal_handler(int sig) {
    fprintf(stderr, "Caught signal %d, cleaning up\n", sig);
    if (cloud_server_fd >= 0) {
        close(cloud_server_fd);
    }
    unlink("/tmp/cloud_manager.sock"); // Clean up any stray UNIX socket
    exit(1);
}

// Check if command is interactive
int is_long_timeout_command(const char *cmd) {
    if (!cmd) return 0;
    for (int i = 0; long_timeout_cmds[i] != NULL; i++) {
        if (strstr(cmd, long_timeout_cmds[i]) == cmd) {
            return 1;
        }
    }
    return 0;
}

// Log metrics to file
void log_metric(const char *metric) {
    if (!metric) {
        fprintf(stderr, "Error: NULL metric in log_metric\n");
        return;
    }
    time_t now = time(NULL);
    char *time_str = ctime(&now);
    if (!time_str) {
        fprintf(stderr, "Failed to get timestamp\n");
        return;
    }
    time_str[strcspn(time_str, "\n")] = '\0';

    FILE *log = fopen(LOG_FILE, "a");
    if (log) {
        flock(fileno(log), LOCK_EX); // Ensure thread-safe writes
        fprintf(log, "%s,%s\n", time_str, metric);
        flock(fileno(log), LOCK_UN);
        fclose(log);
        //fprintf(stderr, "Logged metric: %s\n", metric);
    } else {
        perror("Failed to open log file");
    }
}

// Metric server thread (receives metrics from Device Agent)
void *metric_server_thread(void *arg) {
    cloud_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (cloud_server_fd < 0) {
        perror("Failed to create metric server socket");
        pthread_exit(NULL);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(METRIC_PORT);

    int opt = 1;
    if (setsockopt(cloud_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Failed to set SO_REUSEADDR");
        close(cloud_server_fd);
        pthread_exit(NULL);
    }

    if (bind(cloud_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Failed to bind metric server socket");
        close(cloud_server_fd);
        pthread_exit(NULL);
    }

    if (listen(cloud_server_fd, 10) < 0) {
        perror("Failed to listen on metric server socket");
        close(cloud_server_fd);
        pthread_exit(NULL);
    }

    //printf("Metric server running on %s:%d\n", "127.0.0.1", METRIC_PORT);

    while (1) {
        int client_fd = accept(cloud_server_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("Failed to accept metric connection");
            continue;
        }

        char buffer[MAX_METRIC_SIZE];
        while (1) {
            ssize_t len = recv(client_fd, buffer, MAX_METRIC_SIZE - 1, 0);
            if (len <= 0) {
                if (len == 0) {
                    fprintf(stderr, "Device Agent closed connection\n");
                } else {
                    perror("Failed to receive metric");
                }
                break;
            }
            buffer[len] = '\0';
            // Validate metric format (basic check)
            if (strnlen(buffer, MAX_METRIC_SIZE) == 0 || strchr(buffer, '=') == NULL) {
                fprintf(stderr, "Invalid metric received: %s\n", buffer);
                continue;
            }
        //printf("Cloud Manager received metric: %s\n", buffer);
            log_metric(buffer);
            send(client_fd, "ACK", 3, 0); // Send ACK to Device Agent
        }
        close(client_fd);
    }

    close(cloud_server_fd);
    return NULL;
}

// Set terminal to raw mode
void set_raw_mode(struct termios *original) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, original);
    raw = *original;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

// Restore terminal mode
void restore_mode(struct termios *original) {
    tcsetattr(STDIN_FILENO, TCSANOW, original);
}

// Command client (sends commands to Command Manager and handles output)
void run_command_client() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    struct termios original;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(COMMAND_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to Command Manager failed");
        close(sock);
        return;
    }

    printf("Enter a Linux command to run interactively (e.g., ls, pwd, vim): ");
    fflush(stdout);
    if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
        fprintf(stderr, "Failed to read command\n");
        close(sock);
        return;
    }
    buffer[strcspn(buffer, "\n")] = '\0';

    char original_cmd[BUFFER_SIZE];
    strncpy(original_cmd, buffer, BUFFER_SIZE);
    original_cmd[BUFFER_SIZE - 1] = '\0';

    int is_background = (strlen(original_cmd) > 0 && original_cmd[strlen(original_cmd) - 1] == '&');
    int timeout_seconds = is_long_timeout_command(original_cmd) || is_background ? LONG_TIMEOUT : DEFAULT_TIMEOUT;

    if (send(sock, buffer, strlen(buffer), 0) < 0) {
        perror("Failed to send command");
        close(sock);
        return;
    }

    set_raw_mode(&original);

    fd_set fds;
    struct timeval timeout;
    time_t last_activity = time(NULL);

    while (1) {
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(sock, &fds);
        int maxfd = (STDIN_FILENO > sock ? STDIN_FILENO : sock) + 1;

        timeout.tv_sec = 1; // Check every second
        timeout.tv_usec = 0;

        int activity = select(maxfd, &fds, NULL, NULL, &timeout);
        time_t now = time(NULL);

        if (activity < 0) {
            perror("Select error");
            break;
        }

        if (activity == 0) {
            if (difftime(now, last_activity) >= timeout_seconds) {
                printf("\n[Cloud Manager] Command timed out after %d seconds.\n", timeout_seconds);
                break;
            }
            continue;
        }

        if (FD_ISSET(STDIN_FILENO, &fds)) {
            int n = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (n <= 0) break;
            if (write(sock, buffer, n) < 0) {
                perror("Failed to send input to Command Manager");
                break;
            }
            last_activity = now;
        }

        if (FD_ISSET(sock, &fds)) {
            int n = read(sock, buffer, sizeof(buffer));
            if (n <= 0) {
                if (n == 0) {
                    fprintf(stderr, "Command Manager closed connection\n");
                } else {
                    perror("Failed to read command output");
                }
                break;
            }
            write(STDOUT_FILENO, buffer, n);
            last_activity = now;
        }
    }

    restore_mode(&original);
    close(sock);
}

int main() {
    // Install signal handlers
    signal(SIGSEGV, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    // Start metric server thread (for Device Agent)
    pthread_t metric_thread;
    if (pthread_create(&metric_thread, NULL, metric_server_thread, NULL) != 0) {
        perror("Failed to start metric server thread");
        return 1;
    }

    // Run command client in main thread
    printf("Starting Cloud Manager CLI...\n");
    while (1) {
        run_command_client();
        printf("\nWould you like to run another command? (y/n): ");
        char response;
        scanf(" %c", &response);
        getchar(); // Clear newline
        if (response != 'y' && response != 'Y') {
            break;
        }
    }

    // Cleanup
    if (cloud_server_fd >= 0) {
        close(cloud_server_fd);
    }
    pthread_cancel(metric_thread);
    pthread_join(metric_thread, NULL);

    return 0;
}