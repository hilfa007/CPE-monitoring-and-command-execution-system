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
#include <microhttpd.h>


#define BUFFER_SIZE 1024
#define METRIC_PORT 8080
#define COMMAND_PORT 8081
#define ALARM_PORT 8082
#define MAX_METRIC_SIZE 256
#define DEFAULT_TIMEOUT 10
#define LONG_TIMEOUT 300
#define DEFAULT_RESPONSE_TIMEOUT 15  // 15 seconds for non-interactive commands
#define LONG_RESPONSE_TIMEOUT 310   // 310 seconds for interactive commands
#define LOG_FILE "cloud_metrics.log"

const char *long_timeout_cmds[] = {"vim", "nano", "vi", "cat", "less", "more", "man", NULL};

int cloud_server_fd = -1;
struct MHD_Daemon *alarm_daemon = NULL;

// Synchronization primitives
pthread_mutex_t alert_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alert_cond = PTHREAD_COND_INITIALIZER;
int alert_active = 0; // Flag to indicate if an alert is being processed

void signal_handler(int sig) {
    fprintf(stderr, "Caught signal %d, cleaning up\n", sig);
    if (cloud_server_fd >= 0) {
        close(cloud_server_fd);
    }
    if (alarm_daemon) {
        MHD_stop_daemon(alarm_daemon);
    }
    unlink("/tmp/cloud_manager.sock");
    exit(1);
}

int is_long_timeout_command(const char *cmd) {
    if (!cmd) return 0;
    for (int i = 0; long_timeout_cmds[i]; i++) {
        if (strstr(cmd, long_timeout_cmds[i]) == cmd) {
            return 1;
        }
    }
    return 0;
}

void log_metric(const char *metric) {
    if (!metric) {
        fprintf(stderr, "Error: NULL metric\n");
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
        flock(fileno(log), LOCK_EX);
        fprintf(log, "%s,%s\n", time_str, metric);
        flock(fileno(log), LOCK_UN);
        fclose(log);
    } else {
        perror("Failed to open log file");
    }
}

enum MHD_Result alarm_handler(void *cls, struct MHD_Connection *connection,
                             const char *url, const char *method, const char *version,
                             const char *upload_data, size_t *upload_data_size, void **con_cls) {
    if (strcmp(method, "POST") != 0 || strcmp(url, "/alarm") != 0) {
        struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
        int ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
        MHD_destroy_response(response);
        return ret;
    }

    if (*upload_data_size) {
        // Lock mutex to signal alert is active
        pthread_mutex_lock(&alert_mutex);
        alert_active = 1;

        char *data = strndup(upload_data, *upload_data_size);
        if (!data) {
            alert_active = 0;
            pthread_cond_broadcast(&alert_cond);
            pthread_mutex_unlock(&alert_mutex);
            return MHD_YES;
        }

        char *message_start = strstr(data, "\"message\":\"");
        if (message_start) {
            message_start += 11;
            char *message_end = strchr(message_start, '"');
            if (message_end) {
                *message_end = '\0';
                // Get current timestamp
                time_t now = time(NULL);
                char *time_str = ctime(&now);
                if (time_str) {
                    time_str[strcspn(time_str, "\n")] = '\0';
                    // Print alert with timestamp and exclusive access to stdout
                    printf("\n[ALARM RECEIVED] %s: %s\n", time_str, message_start);
                    fflush(stdout);
                } else {
                    // Fallback if timestamp fails
                    printf("\n[ALARM RECEIVED] %s\n", message_start);
                    fflush(stdout);
                }
            }
        }
        free(data);

        const char *response_body = "{\"status\":\"OK\"}";
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(response_body),
                                                                       (void *)response_body,
                                                                       MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Content-Type", "application/json");
        MHD_add_response_header(response, "Content-Length", "15");

        *upload_data_size = 0;
        int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);

        // Signal command client to resume and release mutex
        alert_active = 0;
        pthread_cond_broadcast(&alert_cond);
        pthread_mutex_unlock(&alert_mutex);
        return ret;
    }

    return MHD_YES;
}

void *alarm_server_thread(void *arg) {
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(ALARM_PORT);

    alarm_daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, ALARM_PORT, NULL, NULL,
                                    &alarm_handler, NULL,
                                    MHD_OPTION_SOCK_ADDR, &addr,
                                    MHD_OPTION_END);
    if (!alarm_daemon) {
        fprintf(stderr, "ERROR: Failed to start alarm server on port %d: %s\n", ALARM_PORT, strerror(errno));
        pthread_exit(NULL);
    }
    while (1) {
        sleep(1);
    }
    return NULL;
}

void *metric_server_thread(void *arg) {
    cloud_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (cloud_server_fd < 0) {
        perror("Failed to create metric server socket");
        pthread_exit(NULL);
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(METRIC_PORT);

    int opt = 1;
    if (setsockopt(cloud_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(cloud_server_fd);
        pthread_exit(NULL);
    }

    if (bind(cloud_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind metric server socket");
        close(cloud_server_fd);
        pthread_exit(NULL);
    }

    if (listen(cloud_server_fd, 10) < 0) {
        perror("Listen on metric server socket");
        close(cloud_server_fd);
        pthread_exit(NULL);
    }

    while (1) {
        int client_fd = accept(cloud_server_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("Accept metric connection");
            continue;
        }

        char buffer[MAX_METRIC_SIZE];
        while (1) {
            ssize_t len = recv(client_fd, buffer, MAX_METRIC_SIZE - 1, 0);
            if (len <= 0) {
                if (len == 0) {
                    fprintf(stderr, "Device Agent closed connection\n");
                } else {
                    perror("Receive metric");
                }
                break;
            }
            buffer[len] = '\0';
            if (!strnlen(buffer, MAX_METRIC_SIZE) || !strchr(buffer, '=')) {
                fprintf(stderr, "Invalid metric: %s\n", buffer);
                continue;
            }
            log_metric(buffer);
            send(client_fd, "ACK", 3, 0);
        }
        close(client_fd);
    }
    close(cloud_server_fd);
    return NULL;
}

void set_raw_mode(struct termios *original) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, original);
    raw = *original;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

void restore_mode(struct termios *original) {
    tcsetattr(STDIN_FILENO, TCSANOW, original);
}

void run_command_client() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(COMMAND_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to Command Manager failed");
        close(sock);
        return;
    }

    struct termios original;
    tcgetattr(STDIN_FILENO, &original); // Save original terminal settings

    char buffer[BUFFER_SIZE];
    int command_sent = 0; // Track if a command has been sent
    int timeout_seconds = DEFAULT_TIMEOUT; // Initialize with default inactivity timeout
    int response_timeout_set = 0; // Track if response timeout has been applied

    while (1) {
        // Check if an alert is active before prompting for a command
        pthread_mutex_lock(&alert_mutex);
        int was_alert_active = alert_active;
        while (alert_active) {
            pthread_cond_wait(&alert_cond, &alert_mutex);
        }
        pthread_mutex_unlock(&alert_mutex);

        // If an alert was just processed, reset to prompt for a new command
        if (was_alert_active) {
            command_sent = 0;
            response_timeout_set = 0; // Reset response timeout flag
            restore_mode(&original);
        }

        // Prompt for command only if no command has been sent yet
        if (!command_sent) {
            // Ensure terminal is in canonical mode with echo enabled
            struct termios temp = original;
            tcsetattr(STDIN_FILENO, TCSANOW, &temp);

            printf("\rEnter a Linux command to run interactively (e.g., ls, pwd, vim): ");
            fflush(stdout);

            // Read command
            if (!fgets(buffer, BUFFER_SIZE, stdin)) {
                fprintf(stderr, "Failed to read command\n");
                restore_mode(&original);
                close(sock);
                return;
            }
            buffer[strcspn(buffer, "\n")] = '\0';

            // Skip empty commands
            if (strlen(buffer) == 0) {
                continue;
            }

            char original_cmd[BUFFER_SIZE];
            strncpy(original_cmd, buffer, BUFFER_SIZE);
            original_cmd[BUFFER_SIZE - 1] = '\0';

            int is_background = (strlen(original_cmd) > 0 && original_cmd[strlen(original_cmd) - 1] == '&');
            timeout_seconds = is_long_timeout_command(original_cmd) || is_background ? LONG_TIMEOUT : DEFAULT_TIMEOUT;

            // Set response timeout based on command type
            struct timeval response_timeout;
            response_timeout.tv_sec = is_long_timeout_command(original_cmd) ? LONG_RESPONSE_TIMEOUT : DEFAULT_RESPONSE_TIMEOUT;
            response_timeout.tv_usec = 0;
            if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &response_timeout, sizeof(response_timeout)) < 0) {
                perror("Failed to set response timeout");
                restore_mode(&original);
                close(sock);
                return;
            }

            if (send(sock, buffer, strlen(buffer), 0) < 0) {
                perror("Failed to send command");
                restore_mode(&original);
                close(sock);
                return;
            }
            command_sent = 1; // Mark command as sent

            // Switch to raw mode for command execution
            set_raw_mode(&original);
        }

        fd_set fds;
        struct timeval timeout;
        time_t last_activity = time(NULL);

        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(sock, &fds);
        int maxfd = (STDIN_FILENO > sock ? STDIN_FILENO : sock) + 1;

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(maxfd, &fds, NULL, NULL, &timeout);
        time_t now = time(NULL);

        if (activity < 0) {
            perror("Select error");
            restore_mode(&original);
            close(sock);
            return;
        }

        if (activity == 0) {
            if (difftime(now, last_activity) >= timeout_seconds) {
                printf("\n[Cloud Manager] Command timed out after %d seconds.\n", timeout_seconds);
                restore_mode(&original);
                close(sock);
                return;
            }
            continue;
        }

        if (FD_ISSET(STDIN_FILENO, &fds)) {
            int n = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (n <= 0) {
                restore_mode(&original);
                close(sock);
                return;
            }
            if (write(sock, buffer, n) < 0) {
                perror("Failed to send input to Command Manager");
                restore_mode(&original);
                close(sock);
                return;
            }
            last_activity = now;
        }

        if (FD_ISSET(sock, &fds)) {
            int n = read(sock, buffer, sizeof(buffer));
            if (n <= 0) {
                if (n == 0) {
                    fprintf(stderr, "Command Manager closed connection\n");
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Response timeout occurred
                    printf("\n[Cloud Manager] No response from Command Manager after %d seconds.\n",
                           is_long_timeout_command(buffer) ? LONG_RESPONSE_TIMEOUT : DEFAULT_RESPONSE_TIMEOUT);
                } else {
                    perror("Failed to read command output");
                }
                restore_mode(&original);
                close(sock);
                return;
            }
            // Once we receive the first response, disable the response timeout to allow normal operation
            if (!response_timeout_set) {
                struct timeval disable_timeout;
                disable_timeout.tv_sec = 0;
                disable_timeout.tv_usec = 0;
                setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &disable_timeout, sizeof(disable_timeout));
                response_timeout_set = 1;
            }
            // Ensure output is thread-safe
            pthread_mutex_lock(&alert_mutex);
            write(STDOUT_FILENO, buffer, n);
            pthread_mutex_unlock(&alert_mutex);
            last_activity = now;
        }
    }

    restore_mode(&original);
    close(sock);
}

int main() {
    signal(SIGSEGV, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    pthread_t metric_thread;
    if (pthread_create(&metric_thread, NULL, metric_server_thread, NULL)) {
        perror("Failed to start metric server thread");
        return 1;
    }

    pthread_t alarm_thread;
    if (pthread_create(&alarm_thread, NULL, alarm_server_thread, NULL)) {
        perror("Failed to start alarm server thread");
        return 1;
    }

    printf("Starting Cloud Manager CLI...\n");
    while (1) {
        run_command_client();
    }

    if (cloud_server_fd >= 0) {
        close(cloud_server_fd);
    }
    if (alarm_daemon) {
        MHD_stop_daemon(alarm_daemon);
    }
    pthread_cancel(metric_thread);
    pthread_cancel(alarm_thread);
    pthread_join(metric_thread, NULL);
    pthread_join(alarm_thread, NULL);

    // Cleanup synchronization primitives
    pthread_mutex_destroy(&alert_mutex);
    pthread_cond_destroy(&alert_cond);
    return 0;
}