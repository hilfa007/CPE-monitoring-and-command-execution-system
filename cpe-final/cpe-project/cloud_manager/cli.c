// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <arpa/inet.h>
// #include <errno.h>
// #include <signal.h>
// #include <poll.h>

// #define CLOUD_HOST "127.0.0.1"
// #define METRIC_PORT 8083   // Cloud Manager server for metrics/alarms
// #define COMMAND_PORT 8081  // Command Manager
// #define MAX_MESSAGE_SIZE 2048

// // Global variables for sockets
// int metric_sock = -1;
// int command_sock = -1;

// // Signal handler for graceful exit
// void signal_handler(int sig) {
//     fprintf(stderr, "Caught signal %d, exiting\n", sig);
//     if (metric_sock >= 0) {
//         close(metric_sock);
//         fprintf(stderr, "Closed metric socket\n");
//     }
//     if (command_sock >= 0) {
//         close(command_sock);
//         fprintf(stderr, "Closed command socket\n");
//     }
//     exit(0);
// }

// // Connect to a server (Cloud Manager or Command Manager)
// int connect_to_server(const char *host, int port) {
//     int sock = socket(AF_INET, SOCK_STREAM, 0);
//     if (sock < 0) {
//         perror("Failed to create socket");
//         return -1;
//     }

//     struct sockaddr_in addr;
//     memset(&addr, 0, sizeof(addr));
//     addr.sin_family = AF_INET;
//     addr.sin_port = htons(port);
//     if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
//         fprintf(stderr, "Invalid address: %s\n", host);
//         close(sock);
//         return -1;
//     }

//     if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
//         fprintf(stderr, "Connection to %s:%d failed: %s\n", host, port, strerror(errno));
//         close(sock);
//         return -1;
//     }

//     return sock;
// }

// // Extract alarm message from JSON
// char *extract_alarm_message(const char *json) {
//     // Look for "message":"..."
//     const char *message_key = "\"message\":\"";
//     char *start = strstr(json, message_key);
//     if (!start) {
//         return NULL;
//     }
//     start += strlen(message_key);

//     // Find closing quote
//     char *end = strchr(start, '"');
//     if (!end) {
//         return NULL;
//     }

//     // Allocate and copy message
//     size_t len = end - start;
//     char *message = malloc(len + 1);
//     if (!message) {
//         fprintf(stderr, "Failed to allocate memory for alarm message\n");
//         return NULL;
//     }
//     strncpy(message, start, len);
//     message[len] = '\0';
//     return message;
// }

// // Check if message is a JSON alarm
// int is_json_alarm(const char *message) {
//     // Basic check: starts with { and contains "type":"alarm"
//     return message[0] == '{' && strstr(message, "\"type\":\"alarm\"");
// }

// int main() {
//     // Set up signal handlers
//     signal(SIGSEGV, signal_handler);
//     signal(SIGTERM, signal_handler);
//     signal(SIGINT, signal_handler);
//     signal(SIGPIPE, SIG_IGN);

//     // Connect to Cloud Manager server for alarms
//     metric_sock = connect_to_server(CLOUD_HOST, METRIC_PORT);
//     if (metric_sock < 0) {
//         fprintf(stderr, "Failed to connect to alarm server\n");
//         exit(1);
//     }
//     printf("Connected to alarm server on %s:%d\n", CLOUD_HOST, METRIC_PORT);

//     // Connect to Command Manager for commands
//     command_sock = connect_to_server(CLOUD_HOST, COMMAND_PORT);
//     if (command_sock < 0) {
//         fprintf(stderr, "Failed to connect to command server\n");
//         close(metric_sock);
//         exit(1);
//     }
//     printf("Connected to command server on %s:%d\n", CLOUD_HOST, COMMAND_PORT);

//     printf("\n=== Cloud Manager CLI ===\n");
//     printf("Type a command (e.g., 'ls -l') or 'exit' to quit.\n");
//     printf("Alarms will appear automatically below.\n");
//     printf("=================================\n\n> ");
//     fflush(stdout);

//     // Set up poll for alarm socket, command socket, and stdin
//     struct pollfd fds[3];
//     fds[0].fd = metric_sock;
//     fds[0].events = POLLIN;
//     fds[1].fd = command_sock;
//     fds[1].events = POLLIN;
//     fds[2].fd = fileno(stdin);
//     fds[2].events = POLLIN;

//     char buffer[MAX_MESSAGE_SIZE];
//     while (1) {
//         int ret = poll(fds, 3, -1);
//         if (ret < 0) {
//             perror("Poll failed");
//             break;
//         }

//         // Alarms from Cloud Manager server
//         if (fds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
//             ssize_t len = recv(metric_sock, buffer, MAX_MESSAGE_SIZE - 1, 0);
//             if (len <= 0) {
//                 if (len == 0) {
//                     fprintf(stderr, "Alarm server closed connection\n");
//                 } else {
//                     fprintf(stderr, "Failed to receive alarm: %s\n", strerror(errno));
//                 }
//                 break;
//             }
//             buffer[len] = '\0';

//             // Only process JSON alarms, ignore metrics
//             if (is_json_alarm(buffer)) {
//                 char *message = extract_alarm_message(buffer);
//                 if (message) {
//                     printf("\nAlert!! %s\n> ", message);
//                     free(message);
//                 } else {
//                     printf("\nAlert!! Unknown alarm received\n> ");
//                 }
//                 fflush(stdout);
//             }
//             // Metrics (non-JSON) are ignored
//         }

//         // Command output from Command Manager
//         if (fds[1].revents & (POLLIN | POLLERR | POLLHUP)) {
//             ssize_t len = recv(command_sock, buffer, MAX_MESSAGE_SIZE - 1, 0);
//             if (len <= 0) {
//                 if (len == 0) {
//                     fprintf(stderr, "Command server closed connection\n");
//                 } else {
//                     fprintf(stderr, "Failed to receive command output: %s\n", strerror(errno));
//                 }
//                 break;
//             }
//             buffer[len] = '\0';
//             printf("\n[Command Output]: %s> ", buffer);
//             fflush(stdout);
//         }

//         // User input for commands
//         if (fds[2].revents & POLLIN) {
//             if (fgets(buffer, MAX_MESSAGE_SIZE, stdin) == NULL) {
//                 fprintf(stderr, "Failed to read command\n");
//                 break;
//             }
//             buffer[strcspn(buffer, "\n")] = '\0'; // Remove newline

//             if (strcmp(buffer, "exit") == 0) {
//                 printf("Exiting CLI\n");
//                 break;
//             }

//             if (strlen(buffer) == 0) {
//                 printf("> ");
//                 fflush(stdout);
//                 continue;
//             }

//             // Send command to Command Manager
//             char cmd_with_newline[MAX_MESSAGE_SIZE];
//             snprintf(cmd_with_newline, sizeof(cmd_with_newline), "%s\n", buffer);
//             if (send(command_sock, cmd_with_newline, strlen(cmd_with_newline), 0) < 0) {
//                 fprintf(stderr, "Failed to send command: %s\n", strerror(errno));
//                 break;
//             }
//             printf("[Command Sent]: %s\n> ", buffer);
//             fflush(stdout);
//         }
//     }

//     // Cleanup
//     if (metric_sock >= 0) {
//         close(metric_sock);
//         fprintf(stderr, "Closed alarm socket\n");
//     }
//     if (command_sock >= 0) {
//         close(command_sock);
//         fprintf(stderr, "Closed command socket\n");
//     }
//     printf("Disconnected from servers\n");
//     return 0;
// }



// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <arpa/inet.h>
// #include <errno.h>
// #include <signal.h>
// #include <poll.h>

// #define CLOUD_HOST "127.0.0.1"
// #define METRIC_PORT 8083   // Cloud Manager server for alarms
// #define COMMAND_PORT 8081  // Command Manager
// #define MAX_MESSAGE_SIZE 2048

// // Global variables for sockets
// int metric_sock = -1;
// int command_sock = -1;

// // Signal handler for graceful exit
// void signal_handler(int sig) {
//     fprintf(stderr, "Caught signal %d, exiting\n", sig);
//     if (metric_sock >= 0) {
//         close(metric_sock);
//         fprintf(stderr, "Closed alarm socket\n");
//     }
//     if (command_sock >= 0) {
//         close(command_sock);
//         fprintf(stderr, "Closed command socket\n");
//     }
//     exit(0);
// }

// // Print prompt
// void print_prompt() {
//     printf("> ");
//     fflush(stdout);
// }

// // Connect to a server (Cloud Manager or Command Manager)
// int connect_to_server(const char *host, int port) {
//     int sock = socket(AF_INET, SOCK_STREAM, 0);
//     if (sock < 0) {
//         fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
//         return -1;
//     }

//     struct sockaddr_in addr;
//     memset(&addr, 0, sizeof(addr));
//     addr.sin_family = AF_INET;
//     addr.sin_port = htons(port);
//     if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
//         fprintf(stderr, "Invalid address: %s\n", host);
//         close(sock);
//         return -1;
//     }

//     if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
//         fprintf(stderr, "Connection to %s:%d failed: %s\n", host, port, strerror(errno));
//         close(sock);
//         return -1;
//     }

//     return sock;
// }

// // Reconnect to Command Manager
// int reconnect_command_server() {
//     if (command_sock >= 0) {
//         close(command_sock);
//         fprintf(stderr, "Closed previous command socket\n");
//     }
//     command_sock = connect_to_server(CLOUD_HOST, COMMAND_PORT);
//     if (command_sock < 0) {
//         fprintf(stderr, "Failed to reconnect to command server\n");
//         return -1;
//     }
//     fprintf(stderr, "Reconnected to command server on %s:%d\n", CLOUD_HOST, COMMAND_PORT);
//     return 0;
// }

// // Extract alarm message from JSON
// char *extract_alarm_message(const char *json) {
//     const char *message_key = "\"message\":\"";
//     char *start = strstr(json, message_key);
//     if (!start) {
//         return NULL;
//     }
//     start += strlen(message_key);

//     char *end = strchr(start, '"');
//     if (!end) {
//         return NULL;
//     }

//     size_t len = end - start;
//     char *message = malloc(len + 1);
//     if (!message) {
//         fprintf(stderr, "Failed to allocate memory for alarm message\n");
//         return NULL;
//     }
//     strncpy(message, start, len);
//     message[len] = '\0';
//     return message;
// }

// // Check if message is a JSON alarm
// int is_json_alarm(const char *message) {
//     return message[0] == '{' && strstr(message, "\"type\":\"alarm\"");
// }

// int main() {
//     // Set up signal handlers
//     signal(SIGSEGV, signal_handler);
//     signal(SIGTERM, signal_handler);
//     signal(SIGINT, signal_handler);
//     signal(SIGPIPE, SIG_IGN);

//     // Connect to Cloud Manager server for alarms
//     metric_sock = connect_to_server(CLOUD_HOST, METRIC_PORT);
//     if (metric_sock < 0) {
//         fprintf(stderr, "Failed to connect to alarm server\n");
//         exit(1);
//     }
//     printf("Connected to alarm server on %s:%d\n", CLOUD_HOST, METRIC_PORT);

//     // Connect to Command Manager for commands
//     if (reconnect_command_server() < 0) {
//         close(metric_sock);
//         exit(1);
//     }
//     printf("Connected to command server on %s:%d\n", CLOUD_HOST, COMMAND_PORT);

//     printf("\n=== Cloud Manager CLI ===\n");
//     printf("Type a command (e.g., 'ls -l') or 'exit' to quit.\n");
//     printf("Alarms will appear automatically below.\n");
//     printf("=================================\n\n");
//     print_prompt();

//     // Set up poll for alarm socket, command socket, and stdin
//     struct pollfd fds[3];
//     fds[0].fd = metric_sock;
//     fds[0].events = POLLIN;
//     fds[1].fd = command_sock;
//     fds[1].events = POLLIN;
//     fds[2].fd = fileno(stdin);
//     fds[2].events = POLLIN;

//     char buffer[MAX_MESSAGE_SIZE];
//     int expect_command_output = 0; // Track if we're waiting for command output

//     while (1) {
//         // Update command socket in poll if reconnected
//         fds[1].fd = command_sock;

//         int ret = poll(fds, 3, -1);
//         if (ret < 0) {
//             perror("Poll failed");
//             break;
//         }

//         // Alarms from Cloud Manager server
//         if (fds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
//             ssize_t len = recv(metric_sock, buffer, MAX_MESSAGE_SIZE - 1, 0);
//             if (len <= 0) {
//                 if (len == 0) {
//                     fprintf(stderr, "Alarm server closed connection\n");
//                 } else {
//                     fprintf(stderr, "Failed to receive alarm: %s\n", strerror(errno));
//                 }
//                 break;
//             }
//             buffer[len] = '\0';

//             // Only process JSON alarms, ignore metrics
//             if (is_json_alarm(buffer)) {
//                 char *message = extract_alarm_message(buffer);
//                 if (message) {
//                     printf("\nAlert!! %s\n", message);
//                     free(message);
//                 } else {
//                     printf("\nAlert!! Unknown alarm received\n");
//                 }
//                 print_prompt();
//             }
//             // Metrics (non-JSON) are ignored
//         }

//         // Command output from Command Manager
//         if (fds[1].revents & (POLLIN | POLLERR | POLLHUP)) {
//             ssize_t len = recv(command_sock, buffer, MAX_MESSAGE_SIZE - 1, 0);
//             if (len <= 0) {
//                 if (len == 0) {
//                     fprintf(stderr, "Command server closed connection\n");
//                 } else {
//                     fprintf(stderr, "Failed to receive command output: %s\n", strerror(errno));
//                 }
//                 // Reconnect to Command Manager
//                 if (reconnect_command_server() < 0) {
//                     fprintf(stderr, "Reconnection failed, exiting\n");
//                     break;
//                 }
//                 expect_command_output = 0;
//                 print_prompt();
//                 continue;
//             }
//             buffer[len] = '\0';
//             printf("\n[Command Output]: %s\n", buffer);
//             expect_command_output = 0; // Command output received
//             print_prompt();
//         }

//         // User input for commands
//         if (fds[2].revents & POLLIN) {
//             if (fgets(buffer, MAX_MESSAGE_SIZE, stdin) == NULL) {
//                 fprintf(stderr, "Failed to read command\n");
//                 break;
//             }
//             buffer[strcspn(buffer, "\n")] = '\0'; // Remove newline

//             if (strcmp(buffer, "exit") == 0) {
//                 printf("Exiting CLI\n");
//                 break;
//             }

//             if (strlen(buffer) == 0) {
//                 print_prompt();
//                 continue;
//             }

//             // Send command to Command Manager
//             char cmd_with_newline[MAX_MESSAGE_SIZE];
//             snprintf(cmd_with_newline, sizeof(cmd_with_newline), "%s\n", buffer);
//             if (send(command_sock, cmd_with_newline, strlen(cmd_with_newline), 0) < 0) {
//                 fprintf(stderr, "Failed to send command: %s\n", strerror(errno));
//                 // Try to reconnect
//                 if (reconnect_command_server() < 0) {
//                     fprintf(stderr, "Reconnection failed, exiting\n");
//                     break;
//                 }
//                 // Resend command after reconnect
//                 if (send(command_sock, cmd_with_newline, strlen(cmd_with_newline), 0) < 0) {
//                     fprintf(stderr, "Failed to resend command: %s\n", strerror(errno));
//                     break;
//                 }
//             }
//             printf("[Command Sent]: %s\n", buffer);
//             expect_command_output = 1; // Expect output
//             print_prompt();
//         }
//     }

//     // Cleanup
//     if (metric_sock >= 0) {
//         close(metric_sock);
//         fprintf(stderr, "Closed alarm socket\n");
//     }
//     if (command_sock >= 0) {
//         close(command_sock);
//         fprintf(stderr, "Closed command socket\n");
//     }
//     printf("Disconnected from servers\n");
//     return 0;
// }


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

#define CLOUD_HOST "127.0.0.1"
#define METRIC_PORT 8083   // Cloud Manager server for alarms
#define COMMAND_PORT 8081  // Command Manager
#define MAX_MESSAGE_SIZE 2048

// Global variables for sockets
int metric_sock = -1;
int command_sock = -1;

// Signal handler for graceful exit
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
void print_prompt() {
    printf("> ");
    fflush(stdout);
}

// Connect to a server (Cloud Manager or Command Manager)
int connect_to_server(const char *host, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", host);
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Connection to %s:%d failed: %s\n", host, port, strerror(errno));
        close(sock);
        return -1;
    }

    return sock;
}

// Reconnect to Command Manager
int reconnect_command_server() {
    if (command_sock >= 0) {
        close(command_sock);
        fprintf(stderr, "Closed previous command socket\n");
    }
    command_sock = connect_to_server(CLOUD_HOST, COMMAND_PORT);
    if (command_sock < 0) {
        fprintf(stderr, "Failed to reconnect to command server\n");
        return -1;
    }
    fprintf(stderr, "Reconnected to command server on %s:%d\n", CLOUD_HOST, COMMAND_PORT);
    return 0;
}

// Get current timestamp in YYYY-MM-DD HH:MM:SS format
char *get_timestamp() {
    static char timestamp[20];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (!tm) {
        fprintf(stderr, "Failed to get local time\n");
        strcpy(timestamp, "Unknown");
        return timestamp;
    }
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);
    return timestamp;
}

// Extract alarm message from JSON
char *extract_alarm_message(const char *json) {
    const char *message_key = "\"message\":\"";
    char *start = strstr(json, message_key);
    if (!start) {
        return NULL;
    }
    start += strlen(message_key);

    char *end = strchr(start, '"');
    if (!end) {
        return NULL;
    }

    size_t len = end - start;
    char *message = malloc(len + 1);
    if (!message) {
        fprintf(stderr, "Failed to allocate memory for alarm message\n");
        return NULL;
    }
    strncpy(message, start, len);
    message[len] = '\0';
    return message;
}

// Check if message is a JSON alarm
int is_json_alarm(const char *message) {
    return message[0] == '{' && strstr(message, "\"type\":\"alarm\"");
}

int main() {
    // Set up signal handlers
    signal(SIGSEGV, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);

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

    printf("\n=== Cloud Manager CLI ===\n");
    printf("Type a command (e.g., 'ls -l') or 'exit' to quit.\n");
    printf("Alarms will appear automatically below.\n");
    printf("=================================\n\n");
    print_prompt();

    // Set up poll for alarm socket, command socket, and stdin
    struct pollfd fds[3];
    fds[0].fd = metric_sock;
    fds[0].events = POLLIN;
    fds[1].fd = command_sock;
    fds[1].events = POLLIN;
    fds[2].fd = fileno(stdin);
    fds[2].events = POLLIN;

    char buffer[MAX_MESSAGE_SIZE];
    int expect_command_output = 0; // Track if we're waiting for command output

    while (1) {
        // Update command socket in poll if reconnected
        fds[1].fd = command_sock;

        int ret = poll(fds, 3, -1);
        if (ret < 0) {
            perror("Poll failed");
            break;
        }

        // Alarms from Cloud Manager server
        if (fds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
            ssize_t len = recv(metric_sock, buffer, MAX_MESSAGE_SIZE - 1, 0);
            if (len <= 0) {
                if (len == 0) {
                    fprintf(stderr, "Alarm server closed connection\n");
                } else {
                    fprintf(stderr, "Failed to receive alarm: %s\n", strerror(errno));
                }
                break;
            }
            buffer[len] = '\0';

            // Only process JSON alarms, ignore metrics
            if (is_json_alarm(buffer)) {
                char *message = extract_alarm_message(buffer);
                if (message) {
                    printf("\n[%s] Alert: %s\n", get_timestamp(), message);
                    free(message);
                } else {
                    printf("\n[%s] Alert: Unknown alarm received\n", get_timestamp());
                }
                print_prompt();
            }
            // Metrics (non-JSON) are ignored
        }

        // Command output from Command Manager
        if (fds[1].revents & (POLLIN | POLLERR | POLLHUP)) {
            ssize_t len = recv(command_sock, buffer, MAX_MESSAGE_SIZE - 1, 0);
            if (len <= 0) {
                if (len == 0) {
                    fprintf(stderr, "Command server closed connection\n");
                } else {
                    fprintf(stderr, "Failed to receive command output: %s\n", strerror(errno));
                }
                // Reconnect to Command Manager
                if (reconnect_command_server() < 0) {
                    fprintf(stderr, "Reconnection failed, exiting\n");
                    break;
                }
                expect_command_output = 0;
                print_prompt();
                continue;
            }
            buffer[len] = '\0';
            printf("\n[Command Output]: %s\n", buffer);
            expect_command_output = 0; // Command output received
            print_prompt();
        }

        // User input for commands
        if (fds[2].revents & POLLIN) {
            if (fgets(buffer, MAX_MESSAGE_SIZE, stdin) == NULL) {
                fprintf(stderr, "Failed to read command\n");
                break;
            }
            buffer[strcspn(buffer, "\n")] = '\0'; // Remove newline

            if (strcmp(buffer, "exit") == 0) {
                printf("Exiting CLI\n");
                break;
            }

            if (strlen(buffer) == 0) {
                print_prompt();
                continue;
            }

            // Send command to Command Manager
            char cmd_with_newline[MAX_MESSAGE_SIZE];
            snprintf(cmd_with_newline, sizeof(cmd_with_newline), "%s\n", buffer);
            if (send(command_sock, cmd_with_newline, strlen(cmd_with_newline), 0) < 0) {
                fprintf(stderr, "Failed to send command: %s\n", strerror(errno));
                // Try to reconnect
                if (reconnect_command_server() < 0) {
                    fprintf(stderr, "Reconnection failed, exiting\n");
                    break;
                }
                // Resend command after reconnect
                if (send(command_sock, cmd_with_newline, strlen(cmd_with_newline), 0) < 0) {
                    fprintf(stderr, "Failed to resend command: %s\n", strerror(errno));
                    break;
                }
            }
            printf("[Command Sent]: %s\n", buffer);
            expect_command_output = 1; // Expect output
            print_prompt();
        }
    }

    // Cleanup
    if (metric_sock >= 0) {
        close(metric_sock);
        fprintf(stderr, "Closed alarm socket\n");
    }
    if (command_sock >= 0) {
        close(command_sock);
        fprintf(stderr, "Closed command socket\n");
    }
    printf("Disconnected from servers\n");
    return 0;
}