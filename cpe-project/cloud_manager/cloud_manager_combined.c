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

// Section: Constants and Macros
// Defines constants used throughout the program for buffer sizes, ports, and timeouts
#define BUFFER_SIZE 1024              // Size of the buffer for reading/writing data
#define METRIC_PORT 8080              // Port for metric server
#define COMMAND_PORT 8081             // Port for command server
#define ALARM_PORT 8082               // Port for alarm server
#define MAX_METRIC_SIZE 256           // Maximum size of a metric string
#define DEFAULT_TIMEOUT 10            // Default inactivity timeout in seconds
#define LONG_TIMEOUT 300              // Timeout for long-running commands in seconds
#define DEFAULT_RESPONSE_TIMEOUT 15   // Default response timeout for non-interactive commands
#define LONG_RESPONSE_TIMEOUT 310     // Response timeout for interactive commands
#define LOG_FILE "cloud_metrics.log"  // File name for logging metrics

// Section: Global Variables
// Declares global variables for server file descriptor, HTTP daemon, and synchronization primitives
const char *long_timeout_cmds[] = {"vim", "nano", "vi", "cat", "less", "more", "man", NULL}; // List of commands requiring long timeouts
int cloud_server_fd = -1;           // File descriptor for the metric server socket
struct MHD_Daemon *alarm_daemon = NULL; // Pointer to the MicroHTTPD daemon for alarm server

// Subsection: Synchronization Primitives
// Initializes mutex and condition variable for thread-safe alert handling
pthread_mutex_t alert_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex for synchronizing alert processing
pthread_cond_t alert_cond = PTHREAD_COND_INITIALIZER;    // Condition variable for alert signaling
int alert_active = 0;                                   // Flag indicating if an alert is being processed

// Section: Signal Handler
// Handles cleanup on receiving termination signals (SIGSEGV, SIGTERM)
void signal_handler(int sig) {
    // Print signal information
    fprintf(stderr, "Caught signal %d, cleaning up\n", sig);
    // Close metric server socket if open
    if (cloud_server_fd >= 0) {
        close(cloud_server_fd);
    }
    // Stop alarm server daemon if running
    if (alarm_daemon) {
        MHD_stop_daemon(alarm_daemon);
    }
    // Remove Unix socket file
    unlink("/tmp/cloud_manager.sock");
    // Exit program
    exit(1);
}

// Section: Utility Functions
// Subsection: Command Timeout Check
// Checks if a command is in the list of long-timeout commands
int is_long_timeout_command(const char *cmd) {
    // Return 0 if command is NULL
    if (!cmd) return 0;
    // Iterate through long-timeout command list
    for (int i = 0; long_timeout_cmds[i]; i++) {
        // Check if command starts with a long-timeout command
        if (strstr(cmd, long_timeout_cmds[i]) == cmd) {
            return 1;
        }
    }
    return 0;
}

// Subsection: Metric Logging
// Logs a metric to a file with a timestamp
void log_metric(const char *metric) {
    // Check for NULL metric
    if (!metric) {
        fprintf(stderr, "Error: NULL metric\n");
        return;
    }
    // Get current time
    time_t now = time(NULL);
    char *time_str = ctime(&now);
    // Check for timestamp failure
    if (!time_str) {
        fprintf(stderr, "Failed to get timestamp\n");
        return;
    }
    // Remove newline from timestamp
    time_str[strcspn(time_str, "\n")] = '\0';

    // Open log file in append mode
    FILE *log = fopen(LOG_FILE, "a");
    if (log) {
        // Lock file for exclusive access
        flock(fileno(log), LOCK_EX);
        // Write timestamp and metric to file
        fprintf(log, "%s,%s\n", time_str, metric);
        // Unlock file
        flock(fileno(log), LOCK_UN);
        // Close file
        fclose(log);
    } else {
        // Print error if file cannot be opened
        perror("Failed to open log file");
    }
}

// Section: Alarm Server
// Subsection: Alarm Handler
// Handles HTTP POST requests to /alarm endpoint
enum MHD_Result alarm_handler(void *cls, struct MHD_Connection *connection,
                             const char *url, const char *method, const char *version,
                             const char *upload_data, size_t *upload_data_size, void **con_cls) {
    // Check if method is POST and URL is /alarm
    if (strcmp(method, "POST") != 0 || strcmp(url, "/alarm") != 0) {
        // Create empty response for invalid requests
        struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
        // Queue 405 Method Not Allowed response
        int ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
        // Destroy response object
        MHD_destroy_response(response);
        return ret;
    }

    // Process incoming data
    if (*upload_data_size) {
        // Lock mutex to indicate alert processing
        pthread_mutex_lock(&alert_mutex);
        alert_active = 1;

        // Duplicate upload data
        char *data = strndup(upload_data, *upload_data_size);
        if (!data) {
            // Reset alert flag and signal condition if allocation fails
            alert_active = 0;
            pthread_cond_broadcast(&alert_cond);
            pthread_mutex_unlock(&alert_mutex);
            return MHD_YES;
        }

        // Extract message from JSON payload
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
                    // Remove newline from timestamp
                    time_str[strcspn(time_str, "\n")] = '\0';
                    // Print alert with timestamp
                    printf("\n[ALARM RECEIVED] %s: %s\n", time_str, message_start);
                    fflush(stdout);
                } else {
                    // Fallback printing without timestamp
                    printf("\n[ALARM RECEIVED] %s\n", message_start);
                    fflush(stdout);
                }
            }
        }
        // Free duplicated data
        free(data);

        // Create JSON response
        const char *response_body = "{\"status\":\"OK\"}";
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(response_body),
                                                                       (void *)response_body,
                                                                       MHD_RESPMEM_PERSISTENT);
        // Add response headers
        MHD_add_response_header(response, "Content-Type", "application/json");
        MHD_add_response_header(response, "Content-Length", "15");

        // Clear upload data size
        *upload_data_size = 0;
        // Queue 200 OK response
        int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        // Destroy response object
        MHD_destroy_response(response);

        // Reset alert flag and signal condition
        alert_active = 0;
        pthread_cond_broadcast(&alert_cond);
        pthread_mutex_unlock(&alert_mutex);
        return ret;
    }

    return MHD_YES;
}

// Subsection: Alarm Server Thread
// Runs the HTTP server for handling alarms
void *alarm_server_thread(void *arg) {
    // Initialize server address structure
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(ALARM_PORT);

    // Start MicroHTTPD daemon
    alarm_daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, ALARM_PORT, NULL, NULL,
                                    &alarm_handler, NULL,
                                    MHD_OPTION_SOCK_ADDR, &addr,
                                    MHD_OPTION_END);
    if (!alarm_daemon) {
        // Print error and exit thread if daemon fails to start
        fprintf(stderr, "ERROR: Failed to start alarm server on port %d: %s\n", ALARM_PORT, strerror(errno));
        pthread_exit(NULL);
    }
    // Keep thread running
    while (1) {
        sleep(1);
    }
    return NULL;
}

// Section: Metric Server
// Subsection: Metric Server Thread
// Runs the TCP server for receiving and logging metrics
void *metric_server_thread(void *arg) {
    // Create metric server socket
    cloud_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (cloud_server_fd < 0) {
        // Print error and exit thread if socket creation fails
        perror("Failed to create metric server socket");
        pthread_exit(NULL);
    }

    // Initialize server address structure
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(METRIC_PORT);

    // Set socket option to reuse address
    int opt = 1;
    if (setsockopt(cloud_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        // Print error and exit thread if setsockopt fails
        perror("setsockopt SO_REUSEADDR");
        close(cloud_server_fd);
        pthread_exit(NULL);
    }

    // Bind socket to address
    if (bind(cloud_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        // Print error and exit thread if bind fails
        perror("Bind metric server socket");
        close(cloud_server_fd);
        pthread_exit(NULL);
    }

    // Listen for incoming connections
    if (listen(cloud_server_fd, 10) < 0) {
        // Print error and exit thread if listen fails
        perror("Listen on metric server socket");
        close(cloud_server_fd);
        pthread_exit(NULL);
    }

    // Main loop to accept client connections
    while (1) {
        // Accept new client connection
        int client_fd = accept(cloud_server_fd, NULL, NULL);
        if (client_fd < 0) {
            // Print error and continue loop if accept fails
            perror("Accept metric connection");
            continue;
        }

        // Buffer for receiving metrics
        char buffer[MAX_METRIC_SIZE];
        // Loop to receive data from client
        while (1) {
            // Receive data
            ssize_t len = recv(client_fd, buffer, MAX_METRIC_SIZE - 1, 0);
            if (len <= 0) {
                // Handle client disconnection or error
                if (len == 0) {
                    fprintf(stderr, "Device Agent closed connection\n");
                } else {
                    perror("Receive metric");
                }
                break;
            }
            // Null-terminate received data
            buffer[len] = '\0';
            // Validate metric format
            if (!strnlen(buffer, MAX_METRIC_SIZE) || !strchr(buffer, '=')) {
                fprintf(stderr, "Invalid metric: %s\n", buffer);
                continue;
            }
            // Log valid metric
            log_metric(buffer);
            // Send acknowledgment to client
            send(client_fd, "ACK", 3, 0);
        }
        // Close client connection
        close(client_fd);
    }
    // Close server socket
    close(cloud_server_fd);
    return NULL;
}

// Section: Terminal Mode Functions
// Subsection: Set Raw Mode
// Configures terminal to raw mode for non-canonical input
void set_raw_mode(struct termios *original) {
    // Get current terminal attributes
    struct termios raw;
    tcgetattr(STDIN_FILENO, original);
    raw = *original;
    // Disable canonical mode and echo
    raw.c_lflag &= ~(ICANON | ECHO);
    // Apply new attributes
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

// Subsection: Restore Mode
// Restores original terminal settings
void restore_mode(struct termios *original) {
    // Restore saved terminal attributes
    tcsetattr(STDIN_FILENO, TCSANOW, original);
}

// Section: Command Client
// Subsection: Run Command Client
// Implements the client for sending commands and receiving output
void run_command_client() {
    // Create client socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        // Print error and return if socket creation fails
        perror("Socket creation failed");
        return;
    }

    // Initialize server address
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(COMMAND_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to command server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        // Print error and cleanup if connection fails
        perror("Connection to Command Manager failed");
        close(sock);
        return;
    }

    // Save original terminal settings
    struct termios original;
    tcgetattr(STDIN_FILENO, &original);

    // Initialize variables for command processing
    char buffer[BUFFER_SIZE];
    int command_sent = 0; // Flag to track if a command has been sent
    int timeout_seconds = DEFAULT_TIMEOUT; // Default inactivity timeout
    int response_timeout_set = 0; // Flag to track if response timeout is disabled

    // Main loop for command input and output
    while (1) {
        // Synchronize with alert processing
        pthread_mutex_lock(&alert_mutex);
        int was_alert_active = alert_active;
        while (alert_active) {
            // Wait for alert processing to complete
            pthread_cond_wait(&alert_cond, &alert_mutex);
        }
        pthread_mutex_unlock(&alert_mutex);

        // Reset state after an alert
        if (was_alert_active) {
            command_sent = 0;
            response_timeout_set = 0;
            restore_mode(&original);
        }

        // Prompt for new command if none sent
        if (!command_sent) {
            // Restore canonical mode for input
            struct termios temp = original;
            tcsetattr(STDIN_FILENO, TCSANOW, &temp);

            // Display prompt
            printf("\rEnter a Linux command to run interactively (e.g., ls, pwd, vim): ");
            fflush(stdout);

            // Read user input
            if (!fgets(buffer, BUFFER_SIZE, stdin)) {
                fprintf(stderr, "Failed to read command\n");
                restore_mode(&original);
                close(sock);
                return;
            }
            // Remove newline from input
            buffer[strcspn(buffer, "\n")] = '\0';

            // Skip empty commands
            if (strlen(buffer) == 0) {
                continue;
            }

            // Save original command for timeout check
            char original_cmd[BUFFER_SIZE];
            strncpy(original_cmd, buffer, BUFFER_SIZE);
            original_cmd[BUFFER_SIZE - 1] = '\0';

            // Determine if command is background or long-running
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

            // Send command to server
            if (send(sock, buffer, strlen(buffer), 0) < 0) {
                perror("Failed to send command");
                restore_mode(&original);
                close(sock);
                return;
            }
            command_sent = 1; // Mark command as sent

            // Switch to raw mode for interactive input
            set_raw_mode(&original);
        }

        // Initialize select for I/O multiplexing
        fd_set fds;
        struct timeval timeout;
        time_t last_activity = time(NULL);

        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(sock, &fds);
        int maxfd = (STDIN_FILENO > sock ? STDIN_FILENO : sock) + 1;

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        // Check for activity on stdin or socket
        int activity = select(maxfd, &fds, NULL, NULL, &timeout);
        time_t now = time(NULL);

        if (activity < 0) {
            // Handle select error
            perror("Select error");
            restore_mode(&original);
            close(sock);
            return;
        }

        if (activity == 0) {
            // Check for inactivity timeout
            if (difftime(now, last_activity) >= timeout_seconds) {
                printf("\n[Cloud Manager] Command timed out after %d seconds.\n", timeout_seconds);
                restore_mode(&original);
                close(sock);
                return;
            }
            continue;
        }

        // Handle user input
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            int n = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (n <= 0) {
                restore_mode(&original);
                close(sock);
                return;
            }
            // Send input to server
            if (write(sock, buffer, n) < 0) {
                perror("Failed to send input to Command Manager");
                restore_mode(&original);
                close(sock);
                return;
            }
            last_activity = now;
        }

        // Handle server output
        if (FD_ISSET(sock, &fds)) {
            int n = read(sock, buffer, sizeof(buffer));
            if (n <= 0) {
                // Handle connection closure or timeout
                if (n == 0) {
                    fprintf(stderr, "Command Manager closed connection\n");
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    printf("\n[Cloud Manager] No response from Command Manager after %d seconds.\n",
                           is_long_timeout_command(buffer) ? LONG_RESPONSE_TIMEOUT : DEFAULT_RESPONSE_TIMEOUT);
                } else {
                    perror("Failed to read command output");
                }
                restore_mode(&original);
                close(sock);
                return;
            }
            // Disable response timeout after first response
            if (!response_timeout_set) {
                struct timeval disable_timeout;
                disable_timeout.tv_sec = 0;
                disable_timeout.tv_usec = 0;
                setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &disable_timeout, sizeof(disable_timeout));
                response_timeout_set = 1;
            }
            // Print output thread-safely
            pthread_mutex_lock(&alert_mutex);
            write(STDOUT_FILENO, buffer, n);
            pthread_mutex_unlock(&alert_mutex);
            last_activity = now;
        }
    }

    // Cleanup
    restore_mode(&original);
    close(sock);
}

// Section: Main Function
// Entry point of the program
int main() {
    // Set up signal handlers
    signal(SIGSEGV, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    // Start metric server thread
    pthread_t metric_thread;
    if (pthread_create(&metric_thread, NULL, metric_server_thread, NULL)) {
        perror("Failed to start metric server thread");
        return 1;
    }

    // Start alarm server thread
    pthread_t alarm_thread;
    if (pthread_create(&alarm_thread, NULL, alarm_server_thread, NULL)) {
        perror("Failed to start alarm server thread");
        return 1;
    }

    // Print startup message
    printf("Starting Cloud Manager CLI...\n");
    // Run command client in a loop
    while (1) {
        run_command_client();
    }

    // Cleanup resources
    if (cloud_server_fd >= 0) {
        close(cloud_server_fd);
    }
    if (alarm_daemon) {
        MHD_stop_daemon(alarm_daemon);
    }
    // Cancel and join threads
    pthread_cancel(metric_thread);
    pthread_cancel(alarm_thread);
    pthread_join(metric_thread, NULL);
    pthread_join(alarm_thread, NULL);

    // Destroy synchronization primitives
    pthread_mutex_destroy(&alert_mutex);
    pthread_cond_destroy(&alert_cond);
    return 0;
}