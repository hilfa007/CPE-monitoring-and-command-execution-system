// Client program for Cloud Manager CLI, connecting to Command Manager and Cloud Manager servers

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <termios.h>
#include <fcntl.h>

// Constant Definitions
#define CLOUD_HOST "127.0.0.1"          // Host address for both servers
#define METRIC_PORT 8083                // Port for Cloud Manager server (alarms)
#define COMMAND_PORT 8081               // Port for Command Manager server
#define MAX_MESSAGE_SIZE 2048           // Maximum size for messages
#define DEFAULT_TIMEOUT 12              // Default timeout (10s server + 2s buffer)
#define LONG_TIMEOUT 305                // Timeout for long-running/background (300s server + 5s buffer)

// Global variables for sockets
int metric_sock = -1;                   // Socket for Cloud Manager (alarms)
int command_sock = -1;                  // Socket for Command Manager (commands)

// Signal handler for graceful exit
// Closes sockets and exits on SIGSEGV, SIGTERM, or SIGINT
void signal_handler(int sig) {
    fprintf(stderr, "Caught signal %d, exiting\n", sig);
    if (metric_sock >= 0) {
        close(metric_sock);
        fprintf(stderr, "Closed alarm socket\n");
    }
    if (command_sock >= 0) {
        close(command_sock);
        fprintf(stderr, "Closed command socket\n");
    }
    exit(0);
}

// Print prompt
// Displays CLI prompt and flushes stdout
void print_prompt() {
    printf("> ");
    fflush(stdout);
}

// Set terminal to raw mode for non-canonical input
// Disables line buffering and echo for interactive commands
void set_raw_mode(struct termios *original) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, original);  // Save original terminal settings
    raw = *original;
    raw.c_lflag &= ~(ICANON | ECHO);    // Disable canonical mode and echo
    tcsetattr(STDIN_FILENO, TCSANOW, &raw); // Apply raw mode immediately
}

// Restore original terminal settings
// Reverts terminal to saved state
void restore_mode(struct termios *original) {
    tcsetattr(STDIN_FILENO, TCSANOW, original);
}

// Connect to a server (Cloud Manager or Command Manager)
// Creates and connects a TCP socket to the specified host and port
int connect_to_server(const char *host, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0); // Create TCP socket
    if (sock < 0) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));             // Clear address structure
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);                // Convert port to network byte order
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) { // Convert host to binary
        fprintf(stderr, "Invalid address: %s\n", host);
        close(sock);
        return -1;
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Connection to %s:%d failed: %s\n", host, port, strerror(errno));
        close(sock);
        return -1;
    }

    return sock;
}

// Reconnect to Command Manager
// Closes existing command socket and establishes a new connection
int reconnect_command_server() {
    if (command_sock >= 0) {
        close(command_sock);                    // Close existing socket
        fprintf(stderr, "Closed previous command socket\n");
    }
    command_sock = connect_to_server(CLOUD_HOST, COMMAND_PORT); // Connect to server
    if (command_sock < 0) {
        fprintf(stderr, "Failed to reconnect to command server\n");
        return -1;
    }
    fprintf(stderr, "Reconnected to command server on %s:%d\n", CLOUD_HOST, COMMAND_PORT);
    return 0;
}

// Get current timestamp in YYYY-MM-DD HH:MM:SS format
// Returns a static string with the current time
char *get_timestamp() {
    static char timestamp[20];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);            // Get local time
    if (!tm) {
        fprintf(stderr, "Failed to get local time\n");
        strcpy(timestamp, "Unknown");
        return timestamp;
    }
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm); // Format time
    return timestamp;
}

// Extract alarm message from JSON
// Parses JSON to extract the "message" field
char *extract_alarm_message(const char *json) {
    const char *message_key = "\"message\":\"";
    char *start = strstr(json, message_key);    // Find message key
    if (!start) {
        return NULL;
    }
    start += strlen(message_key);               // Move to message content

    char *end = strchr(start, '"');             // Find closing quote
    if (!end) {
        return NULL;
    }

    size_t len = end - start;
    char *message = malloc(len + 1);            // Allocate memory for message
    if (!message) {
        fprintf(stderr, "Failed to allocate memory for alarm message\n");
        return NULL;
    }
    strncpy(message, start, len);               // Copy message
    message[len] = '\0';                        // Null-terminate
    return message;
}

// Check if message is a JSON alarm
// Verifies if message is JSON and contains "type":"alarm"
int is_json_alarm(const char *message) {
    return message[0] == '{' && strstr(message, "\"type\":\"alarm\"");
}

// Check if command is long-running or background
// Identifies commands like vim, top, etc., that require longer timeouts
int is_long_timeout_command(const char *cmd) {
    const char *long_commands[] = {"vim", "nano", "top", "htop", "less", "more", "cat", "vi", "man", NULL};
    for (int i = 0; long_commands[i]; i++) {
        if (strstr(cmd, long_commands[i]) == cmd) { // Match at start of command
            return 1;
        }
    }
    return 0;
}

// Main function
// Implements the Cloud Manager CLI with command and alarm handling
int main() {
    // Set up signal handlers
    signal(SIGSEGV, signal_handler);            // Handle segmentation fault
    signal(SIGTERM, signal_handler);            // Handle termination signal
    signal(SIGINT, signal_handler);             // Handle interrupt (Ctrl+C)
    signal(SIGPIPE, SIG_IGN);                   // Ignore broken pipe

    // Connect to Cloud Manager server for alarms
    metric_sock = connect_to_server(CLOUD_HOST, METRIC_PORT);
    if (metric_sock < 0) {
        fprintf(stderr, "Failed to connect to alarm server\n");
        exit(1);
    }
    printf("Connected to alarm server on %s:%d\n", CLOUD_HOST, METRIC_PORT);

    // Connect to Command Manager for commands
    if (reconnect_command_server() < 0) {
        close(metric_sock);
        exit(1);
    }
    printf("Connected to command server on %s:%d\n", CLOUD_HOST, COMMAND_PORT);

    // Display CLI welcome message
    printf("\n=== Cloud Manager CLI ===\n");
    printf("Type a command (e.g., 'ls -l') or 'exit' to quit.\n");
    printf("Alarms will appear automatically below.\n");
    printf("=================================\n\n");
    print_prompt();

    // Save original terminal settings
    struct termios original;
    tcgetattr(STDIN_FILENO, &original);

    // Set up poll for alarm socket, command socket, and stdin
    struct pollfd fds[3];
    fds[0].fd = metric_sock;                    // Cloud Manager socket
    fds[0].events = POLLIN;
    fds[1].fd = command_sock;                   // Command Manager socket
    fds[1].events = POLLIN;
    fds[2].fd = fileno(stdin);                  // Standard input
    fds[2].events = POLLIN;

    char buffer[MAX_MESSAGE_SIZE];               // Buffer for messages
    int command_sent = 0;                       // Track if command is active
    int is_interactive = 0;                     // Track if command is interactive
    int timeout_seconds = DEFAULT_TIMEOUT;      // Current timeout
    time_t last_activity = time(NULL);          // Track last activity time
    char last_command[MAX_MESSAGE_SIZE] = "";   // Store last command

    // Main event loop
    while (1) {
        // Update command socket in poll if reconnected
        fds[1].fd = command_sock;

        // Poll with 1-second timeout for periodic checks
        int ret = poll(fds, 3, 1000);
        time_t now = time(NULL);

        if (ret < 0) {
            perror("Poll failed");
            restore_mode(&original);
            break;
        }

        if (ret == 0) {
            // Check for inactivity timeout
            if (command_sent && difftime(now, last_activity) >= timeout_seconds) {
                printf("\n[Cloud Manager] Command timed out after %d seconds.\n", timeout_seconds);
                restore_mode(&original);
                command_sent = 0;
                is_interactive = 0;
                print_prompt();
                continue;
            }
        }

        // Handle alarms from Cloud Manager server
        if (fds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
            int alarms_processed = 0;
            // Set metric_sock to non-blocking to drain all alarms
            int flags = fcntl(metric_sock, F_GETFL, 0);
            if (flags == -1) {
                perror("fcntl F_GETFL failed");
                restore_mode(&original);
                break;
            }
            if (fcntl(metric_sock, F_SETFL, flags | O_NONBLOCK) == -1) {
                perror("fcntl F_SETFL failed");
                restore_mode(&original);
                break;
            }
            while (1) {
                // Read alarm message
                ssize_t len = recv(metric_sock, buffer, MAX_MESSAGE_SIZE - 1, 0);
                if (len < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break; // No more data
                    }
                    fprintf(stderr, "Failed to receive alarm: %s\n", strerror(errno));
                    restore_mode(&original);
                    break;
                }
                if (len == 0) {
                    fprintf(stderr, "Alarm server closed connection\n");
                    restore_mode(&original);
                    break;
                }
                buffer[len] = '\0';                 // Null-terminate message

                // Process JSON alarm
                if (is_json_alarm(buffer)) {
                    char *message = extract_alarm_message(buffer);
                    if (message) {
                        printf("\n[%s] Alert: %s\n", get_timestamp(), message);
                        free(message);
                    } else {
                        printf("\n[%s] Alert: Unknown alarm received\n", get_timestamp());
                    }
                    if (command_sent) {
                        restore_mode(&original);    // Reset terminal if command active
                        command_sent = 0;
                        is_interactive = 0;
                    }
                    alarms_processed++;
                }
            }
            // Restore blocking mode
            if (fcntl(metric_sock, F_SETFL, flags & ~O_NONBLOCK) == -1) {
                perror("fcntl F_SETFL failed");
                restore_mode(&original);
                break;
            }
            if (alarms_processed > 0) {
                print_prompt();                 // Reprint prompt after alarms
            }
        }

        // Handle command output from Command Manager
        if (fds[1].revents & (POLLIN | POLLERR | POLLHUP)) {
            // Read command output
            ssize_t len = recv(command_sock, buffer, MAX_MESSAGE_SIZE - 1, 0);
            if (len <= 0) {
                if (len == 0) {
                    fprintf(stderr, "Command server closed connection\n");
                } else {
                    fprintf(stderr, "Failed to receive command output: %s\n", strerror(errno));
                }
                // Attempt to reconnect
                if (reconnect_command_server() < 0) {
                    fprintf(stderr, "Reconnection failed, exiting\n");
                    restore_mode(&original);
                    break;
                }
                command_sent = 0;
                is_interactive = 0;
                restore_mode(&original);
                print_prompt();
                continue;
            }
            buffer[len] = '\0';                 // Null-terminate output

            write(STDOUT_FILENO, buffer, len);  // Write output to stdout
            last_activity = now;                // Update activity time

            // Reset state for non-interactive commands
            if (!is_interactive) {
                command_sent = 0;
                print_prompt();
            }
        }

        // Handle user input for commands
        if (fds[2].revents & POLLIN) {
            if (!command_sent) {
                // Handle new command input
                restore_mode(&original);        // Restore canonical mode
                print_prompt();

                // Read command from stdin
                if (fgets(buffer, MAX_MESSAGE_SIZE, stdin) == NULL) {
                    fprintf(stderr, "Failed to read command\n");
                    restore_mode(&original);
                    break;
                }
                buffer[strcspn(buffer, "\n")] = '\0'; // Remove newline

                if (strcmp(buffer, "exit") == 0) {
                    printf("Exiting CLI\n");
                    restore_mode(&original);
                    break;
                }

                if (strlen(buffer) == 0) {
                    print_prompt();             // Empty input, reprint prompt
                    continue;
                }

                // Store command and set timeout/interactivity
                strncpy(last_command, buffer, MAX_MESSAGE_SIZE - 1);
                last_command[MAX_MESSAGE_SIZE - 1] = '\0';
                int is_background = (strlen(buffer) > 0 && buffer[strlen(buffer) - 1] == '&');
                timeout_seconds = is_long_timeout_command(buffer) || is_background ? LONG_TIMEOUT : DEFAULT_TIMEOUT;
                is_interactive = is_long_timeout_command(buffer);

                // Send command to Command Manager
                char cmd_with_newline[MAX_MESSAGE_SIZE];
                size_t cmd_len = strlen(buffer);
                if (cmd_len >= MAX_MESSAGE_SIZE - 1) {
                    cmd_len = MAX_MESSAGE_SIZE - 2; // Avoid truncation
                }
                snprintf(cmd_with_newline, sizeof(cmd_with_newline), "%.*s\n", (int)cmd_len, buffer);
                if (send(command_sock, cmd_with_newline, strlen(cmd_with_newline), 0) < 0) {
                    fprintf(stderr, "Failed to send command: %s\n", strerror(errno));
                    if (reconnect_command_server() < 0) {
                        fprintf(stderr, "Reconnection failed, exiting\n");
                        restore_mode(&original);
                        break;
                    }
                    // Resend command after reconnect
                    if (send(command_sock, cmd_with_newline, strlen(cmd_with_newline), 0) < 0) {
                        fprintf(stderr, "Failed to resend command: %s\n", strerror(errno));
                        restore_mode(&original);
                        break;
                    }
                }
                printf("[Command Sent]: %s\n", buffer);
                command_sent = 1;
                last_activity = time(NULL);
                if (is_interactive) {
                    set_raw_mode(&original);    // Enable raw mode for interactive input
                }
            } else {
                // Handle interactive input in raw mode
                ssize_t n = read(STDIN_FILENO, buffer, MAX_MESSAGE_SIZE);
                if (n <= 0) {
                    fprintf(stderr, "Failed to read input\n");
                    restore_mode(&original);
                    command_sent = 0;
                    is_interactive = 0;
                    print_prompt();
                    continue;
                }

                // Send interactive input to Command Manager
                if (write(command_sock, buffer, n) < 0) {
                    fprintf(stderr, "Failed to send input to Command Manager: %s\n", strerror(errno));
                    if (reconnect_command_server() < 0) {
                        fprintf(stderr, "Reconnection failed, exiting\n");
                        restore_mode(&original);
                        break;
                    }
                    // Resend input after reconnect
                    if (write(command_sock, buffer, n) < 0) {
                        fprintf(stderr, "Failed to resend input: %s\n", strerror(errno));
                        restore_mode(&original);
                        break;
                    }
                }
                last_activity = time(NULL);     // Update activity time
                // Do not print prompt during interactive session
            }
        }
    }

    // Cleanup
    if (metric_sock >= 0) {
        close(metric_sock);                 // Close alarm socket
        fprintf(stderr, "Closed alarm socket\n");
    }
    if (command_sock >= 0) {
        close(command_sock);                // Close command socket
        fprintf(stderr, "Closed command socket\n");
    }
    restore_mode(&original);                // Restore terminal settings
    printf("Disconnected from servers\n");
    return 0;
}