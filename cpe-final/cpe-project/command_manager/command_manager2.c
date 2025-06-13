// Header Section
// Includes necessary standard and system libraries for socket programming, process handling, and terminal emulation
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

// Constant Definitions
// Defines port number, buffer sizes, log file name, and execution timeout durations
#define PORT 8081
#define BUFFER_SIZE 1024
#define LOG_FILE "commands.log"
#define DEFAULT_EXECUTION_TIMEOUT 10 // Default 10-second execution timeout for most commands
#define LONG_EXECUTION_TIMEOUT 300   // 300-second execution timeout for interactive commands

// Interactive Commands Array
// List of commands that require longer execution timeouts due to their interactive nature
const char *long_timeout_cmds[] = {"vim", "nano", "vi", "cat", "less", "more", "man", NULL};

// Function: is_long_timeout_command
// Checks if a command is in the list of interactive commands requiring a longer timeout
int is_long_timeout_command(const char *cmd) {
    // Iterate through long_timeout_cmds array
    for (int i = 0; long_timeout_cmds[i] != NULL; i++) {
        // Check if command starts with any interactive command
        if (strstr(cmd, long_timeout_cmds[i]) == cmd) {
            return 1; // Return true if found
        }
    }
    return 0; // Return false if not found
}

// Function: is_valid_command
// Performs basic sanitization to prevent command injection
int is_valid_command(const char *cmd) {
    // Check for dangerous characters that could enable command injection
    return strpbrk(cmd, ";|`") == NULL; // Return true if no dangerous characters found
}

// Function: log_command
// Logs executed commands and their output to a file with a timestamp
void log_command(const char *command, const char *output) {
    // Open log file in append mode
    FILE *log = fopen(LOG_FILE, "a");
    if (log) {
        // Get current time for timestamp
        time_t now = time(NULL);
        char *timestamp = ctime(&now);
        if (timestamp) {
            // Remove newline from timestamp
            timestamp[strcspn(timestamp, "\n")] = '\0';
        }
        // Write command, timestamp, and output to log file
        fprintf(log, "\n[%s] Command: %s\nOutput:\n%s\n", timestamp, command, output);
        fclose(log); // Close log file
    } else {
        // Print error if log file cannot be opened
        perror("Failed to open log file");
    }
}

// Function: handle_client
// Handles client connections in a separate thread, processes commands, and manages execution
void *handle_client(void *arg) {
    // Client Socket Initialization
    // Extract client socket from argument and free allocated memory
    int client_socket = *(int *)arg;
    free(arg);

    // Command Reception
    // Read command from client socket
    char command[BUFFER_SIZE];
    int bytes = read(client_socket, command, sizeof(command) - 1);
    if (bytes <= 0) {
        // Close socket if read fails or client disconnects
        close(client_socket);
        return NULL;
    }
    command[bytes] = '\0'; // Null-terminate command string

    // Command Validation
    // Check if command is valid to prevent injection
    if (!is_valid_command(command)) {
        // Send error message to client if command is invalid
        const char *error_msg = "Error: Invalid command\n";
        write(client_socket, error_msg, strlen(error_msg));
        close(client_socket);
        return NULL;
    }

    // Background Job Detection
    // Check if command is to be run in the background
    int len = strlen(command);
    int is_background = 0;
    if (len > 0 && command[len - 1] == '&' && (len == 1 || command[len - 2] != '&')) {
        is_background = 1; // Mark as background job
        command[len - 1] = '\0'; // Remove trailing '&'
    }

    // Timeout Configuration
    // Set execution timeout based on command type
    int execution_timeout = is_long_timeout_command(command) ? LONG_EXECUTION_TIMEOUT : DEFAULT_EXECUTION_TIMEOUT;

    // Output Buffer Allocation
    // Allocate buffer for storing command output
    char *output = malloc(BUFFER_SIZE * 10);
    if (!output) {
        // Handle memory allocation failure
        perror("Memory allocation failed");
        close(client_socket);
        return NULL;
    }
    size_t output_size = BUFFER_SIZE * 10;
    size_t total_output = 0;

    // Background Process Handling
    if (is_background) {
        // Forkpty for Background Process
        // Create a pseudo-terminal for background command execution
        int master_fd;
        pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);
        if (pid < 0) {
            // Handle forkpty failure
            perror("forkpty failed");
            strcpy(output, "Failed to start background command\n");
            total_output = strlen(output);
            write(client_socket, output, total_output);
            goto cleanup;
        }
        if (pid == 0) {
            // Child Process
            // Set up new process group and execute command
            setpgid(0, 0); // Create new process group
            // Generate log file name for output redirection
            char log_file[64];
            snprintf(log_file, sizeof(log_file), "/tmp/bg_%d.log", getpid());
            // Redirect output to log file unless specified in command
            char wrapped_command[BUFFER_SIZE * 2];
            if (strstr(command, ">") == NULL && strstr(command, ">>") == NULL) {
                snprintf(wrapped_command, sizeof(wrapped_command), "%s &> %s &", command, log_file);
            } else {
                snprintf(wrapped_command, sizeof(wrapped_command), "%s &", command);
            }
            // Execute command in bash
            execlp("bash", "bash", "-c", wrapped_command, (char *)NULL);
            perror("execlp failed");
            exit(1);
        }
        // Parent Process
        // Manage background process execution and timeout
        time_t start_time = time(NULL); // Record start time
        // Brief read from pty to capture initial output
        char buffer[BUFFER_SIZE];
        fd_set fds;
        struct timeval timeout;
        FD_ZERO(&fds);
        FD_SET(master_fd, &fds);
        timeout.tv_sec = 1; // Brief wait for initial output
        timeout.tv_usec = 0;
        int activity = select(master_fd + 1, &fds, NULL, NULL, &timeout);
        if (activity > 0 && FD_ISSET(master_fd, &fds)) {
            // Read initial output from pty
            int n = read(master_fd, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                if (total_output + n < output_size) {
                    memcpy(output + total_output, buffer, n);
                    total_output += n;
                }
            }
        }
        // Timeout Check
        // Check if background command exceeds execution timeout
        time_t now = time(NULL);
        if (difftime(now, start_time) >= execution_timeout) {
            printf("Background command exceeded %d seconds execution timeout.\n", execution_timeout);
            kill(-pid, SIGTERM); // Terminate process group
        }
        // Send PID and Log File Info
        // Inform client of process ID and log file location
        char pid_msg[128];
        char log_file[64];
        snprintf(log_file, sizeof(log_file), "/tmp/bg_%d.log", pid);
        snprintf(pid_msg, sizeof(pid_msg), "[1] %d (output redirected to %s)\n", pid, log_file);
        write(client_socket, pid_msg, strlen(pid_msg));
        if (total_output > 0) {
            write(client_socket, output, total_output);
        }
        // Append PID message to output for logging
        if (total_output + strlen(pid_msg) < output_size) {
            memcpy(output + total_output, pid_msg, strlen(pid_msg));
            total_output += strlen(pid_msg);
        }
        close(master_fd); // Close master file descriptor
    } else {
        // Foreground Process Handling
        // Execute command in foreground with terminal emulation
        int master_fd;
        pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);
        if (pid < 0) {
            // Handle forkpty failure
            perror("forkpty failed");
            close(client_socket);
            free(output);
            return NULL;
        }
        if (pid == 0) {
            // Child Process
            // Execute command in bash
            char wrapped_command[BUFFER_SIZE * 2];
            snprintf(wrapped_command, sizeof(wrapped_command), "%s", command);
            execlp("bash", "bash", "-c", wrapped_command, (char *)NULL);
            perror("execlp failed");
            exit(1);
        }
        // Parent Process
        // Manage foreground process execution and communication
        fd_set fds;
        char buffer[BUFFER_SIZE];
        time_t start_time = time(NULL); // Record start time for timeout
        struct timeval timeout;

        // Main Loop
        // Handle communication between client and child process
        while (1) {
            // Initialize file descriptor set for select
            FD_ZERO(&fds);
            FD_SET(client_socket, &fds);
            FD_SET(master_fd, &fds);
            int maxfd = (client_socket > master_fd ? client_socket : master_fd) + 1;

            // Set select timeout
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            // Monitor file descriptors
            int activity = select(maxfd, &fds, NULL, NULL, &timeout);
            time_t now = time(NULL);

            if (activity < 0) {
                // Handle select failure
                perror("select failed");
                break;
            }

            // Client Input Handling
            // Read input from client and send to child process
            if (FD_ISSET(client_socket, &fds)) {
                int n = read(client_socket, buffer, sizeof(buffer));
                if (n <= 0) break; // Break if client disconnects
                write(master_fd, buffer, n); // Forward input to child
            }

            // Child Output Handling
            // Read output from child and send to client
            if (FD_ISSET(master_fd, &fds)) {
                int n = read(master_fd, buffer, sizeof(buffer));
                if (n <= 0) break; // Break if child terminates

                // Resize Output Buffer
                // Expand output buffer if necessary
                if (total_output + n >= output_size) {
                    output_size *= 2;
                    char *new_output = realloc(output, output_size);
                    if (!new_output) {
                        perror("Reallocation failed");
                        break;
                    }
                    output = new_output;
                }

                // Store and Send Output
                // Copy output to buffer and send to client
                memcpy(output + total_output, buffer, n);
                total_output += n;
                write(client_socket, buffer, n);
            }

            // Timeout Check
            // Terminate process if execution timeout is exceeded
            if (difftime(now, start_time) >= execution_timeout) {
                printf("Foreground command exceeded %d seconds execution timeout.\n", execution_timeout);
                kill(-pid, SIGTERM); // Terminate process group
                break;
            }
        }
        close(master_fd); // Close master file descriptor
        waitpid(pid, NULL, 0); // Wait for child process to terminate
    }

    // Cleanup Section
    // Finalize output, log command, and clean up resources
cleanup:
    output[total_output] = '\0'; // Null-terminate output buffer
    log_command(command, output); // Log command and output
    close(client_socket); // Close client socket
    free(output); // Free output buffer
    return NULL;
}

// Main Function
// Sets up the server socket and accepts client connections
int main() {
    // Server Socket Creation
    // Initialize server socket
    int server_fd;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        // Handle socket creation failure
        perror("Socket creation failed");
        return 1;
    }

    // Socket Configuration
    // Configure server address and port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind Socket
    // Bind socket to address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        // Handle bind failure
        perror("Bind failed");
        close(server_fd);
        return 1;
    }

    // Listen for Connections
    // Set socket to listen for incoming connections
    if (listen(server_fd, 10) < 0) {
        // Handle listen failure
        perror("Listen failed");
        close(server_fd);
        return 1;
    }

    // Server Startup Message
    // Indicate server is running
    printf("Server listening on port %d...\n", PORT);

    // Client Connection Loop
    // Accept and handle client connections in separate threads
    while (1) {
        // Allocate Client Socket
        // Allocate memory for client socket descriptor
        int *client_socket = malloc(sizeof(int));
        if (!client_socket) {
            // Handle memory allocation failure
            perror("Memory allocation failed");
            continue;
        }

        // Accept Client Connection
        // Accept incoming client connection
        *client_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (*client_socket < 0) {
            // Handle accept failure
            perror("Accept failed");
            free(client_socket);
            continue;
        }

        // Thread Creation
        // Create thread to handle client
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, client_socket) != 0) {
            // Handle thread creation failure
            perror("Thread creation failed");
            close(*client_socket);
            free(client_socket);
            continue;
        }
        pthread_detach(tid); // Detach thread to allow resource cleanup
    }

    // Server Shutdown
    // Close server socket (unreachable due to infinite loop)
    close(server_fd);
    return 0;
}