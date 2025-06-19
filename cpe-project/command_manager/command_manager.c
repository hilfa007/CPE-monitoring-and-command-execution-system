// Server program for Command Manager, listening on port 8081 to execute client commands
 
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
#include <sys/file.h>
 
// Constant Definitions
#define PORT 8081                      // Port for server to listen on
#define BUFFER_SIZE 1024               // Buffer size for reading commands and output
#define LOG_FILE "commands.log"        // File to log command execution details
#define DEFAULT_EXECUTION_TIMEOUT 10   // Timeout (seconds) for regular commands
#define LONG_EXECUTION_TIMEOUT 300     // Timeout (seconds) for long-running commands
 
// Interactive Commands Array
const char *long_timeout_cmds[] = {"vim", "vi", "cat", "less", "more", "man", NULL};
 
// Function: is_long_timeout_command
// Checks if the command is in the list of long-running commands (e.g., vim, less)
int is_long_timeout_command(const char *cmd) {
    for (int i = 0; long_timeout_cmds[i] != NULL; i++) {
        if (strstr(cmd, long_timeout_cmds[i]) == cmd) {
            return 1; // Command matches at start
        }
    }
    return 0; // Not a long-running command
}

// Function: is_valid_command
// Validates command by checking for dangerous characters (; or `)
int is_valid_command(const char *cmd) {
    return strpbrk(cmd, ";`") == NULL; // Returns true if no ; or ` found
}
 
// Function: log_command
// Logs command, output, timeout duration, and timeout occurrence to a file
void log_command(const char *command, const char *output, int timeout, int timeout_occurred) {
    // Open log file in append mode with 0644 permissions
    int fd = open(LOG_FILE, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) {
        perror("Failed to open log file");
        return;
    }
 
    // Acquire exclusive lock for thread-safe writing
    if (flock(fd, LOCK_EX) < 0) {
        perror("Failed to acquire lock on log file");
        close(fd);
        return;
    }
 
    // Convert file descriptor to FILE stream for formatted writing
    FILE *log = fdopen(fd, "a");
    if (!log) {
        perror("Failed to open log file stream");
        flock(fd, LOCK_UN); // Release lock
        close(fd);
        return;
    }
 
    // Get current timestamp
    time_t now = time(NULL);
    char *timestamp = ctime(&now);
    if (timestamp) {
        timestamp[strcspn(timestamp, "\n")] = '\0'; // Remove newline
    }
 
    // Write log entry with timestamp, command, timeout status, and output
    fprintf(log, "\n[%s] Command: %s\nTimeout Occurred: %s\nOutput:\n%s\n",
            timestamp ? timestamp : "unknown", command,
            timeout_occurred ? "Yes" : "No", output);
 
    // Flush to ensure data is written to disk
    if (fflush(log) != 0) {
        perror("Failed to flush log file");
    }
 
    // Close file, which releases the lock
    if (fclose(log) != 0) {
        perror("Failed to close log file");
    }
}
 
// Function: handle_client
// Handles a single client connection in a separate thread
void *handle_client(void *arg) {
    // Extract client socket and free argument memory
    int client_socket = *(int *)arg;
    free(arg);
 
    // Buffer for receiving command
    char command[BUFFER_SIZE];
    // Read command from client
    int bytes = read(client_socket, command, sizeof(command) - 1);
    if (bytes <= 0) {
        close(client_socket); // Close socket if read fails or client disconnects
        return NULL;
    }
    command[bytes] = '\0'; // Null-terminate command

    //sleep(30); // Simulate delay (30 seconds) for testing for response timeout
    //sleep(400); // Simulate delay (30 seconds) for testing for response timeout for interactive commands
    
    // Validate command to prevent injection
    if (!is_valid_command(command)) {
        const char *error_msg = "Error: Invalid command\n";
        write(client_socket, error_msg, strlen(error_msg)); // Send error to client
        log_command(command, error_msg, 0, 0); // Log invalid command with error message
        close(client_socket);
        return NULL;
    }

   int len = strlen(command);
   
    // Determine timeout based on command type
    int execution_timeout = is_long_timeout_command(command)  ? LONG_EXECUTION_TIMEOUT : DEFAULT_EXECUTION_TIMEOUT;
    int timeout_occurred = 0; // Flag to track if timeout occurred
 
    // Allocate buffer for command output
    char *output = malloc(BUFFER_SIZE * 10);
    if (!output) {
        perror("Memory allocation failed");
        close(client_socket);
        return NULL;
    }
    size_t output_size = BUFFER_SIZE * 10; // Initial output buffer size
    size_t total_output = 0; // Track total output length
 
        // Handle foreground command execution
        int master_fd;
        pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);
        if (pid < 0) {
            perror("forkpty failed");
            close(client_socket);
            free(output);
            return NULL;
        }
        if (pid == 0) {
            // Child process: execute command
            char wrapped_command[BUFFER_SIZE * 2];
            snprintf(wrapped_command, sizeof(wrapped_command), "%s", command);
            execlp("bash", "bash", "-c", wrapped_command, (char *)NULL);
            perror("execlp failed");
            exit(1);
        }
        // Parent process: handle I/O between client and command
        fd_set fds;
        char buffer[BUFFER_SIZE];
        time_t start_time = time(NULL);
        struct timeval timeout;
 
        while (1) {
            FD_ZERO(&fds);
            FD_SET(client_socket, &fds); // Monitor client socket
            FD_SET(master_fd, &fds);    // Monitor pseudo-terminal
            int maxfd = (client_socket > master_fd ? client_socket : master_fd) + 1;//Highest file descriptor to monitor plus one (master_fd + 1).
 
            timeout.tv_sec = 1; // 1-second timeout for select
            timeout.tv_usec = 0;
 
            int activity = select(maxfd, &fds, NULL, NULL, &timeout);//Calls select to wait for activity (readability) on the file descriptors in fds.
            time_t now = time(NULL);
 
            if (activity < 0) {
                perror("select failed");
                break;
            }
            //Checks if client_socket is ready for reading
            if (FD_ISSET(client_socket, &fds)) {
                // Read input from client (e.g., for interactive commands)
                int n = read(client_socket, buffer, sizeof(buffer));
                if (n <= 0) break; // Client disconnected
                write(master_fd, buffer, n); // Forward to command
            }
 
            if (FD_ISSET(master_fd, &fds)) {
                // Read output from command
                int n = read(master_fd, buffer, sizeof(buffer));
                if (n <= 0) break; // Command finished or error
 
                // Resize output buffer if needed
                if (total_output + n >= output_size) {
                    output_size *= 2;
                    char *new_output = realloc(output, output_size);
                    if (!new_output) {
                        perror("Reallocation failed");
                        break;
                    }
                    output = new_output;
                }
 
                // Store and send output
                memcpy(output + total_output, buffer, n);
                total_output += n;
                write(client_socket, buffer, n);
            }
 
            // Check for timeout
            if (difftime(now, start_time) >= execution_timeout) {
                printf("Foreground command exceeded %d seconds execution timeout.\n", execution_timeout);
                kill(-pid, SIGTERM); // Terminate process group
                timeout_occurred = 1;
                break;
            }
        }
        close(master_fd); // Close pseudo-terminal
        waitpid(pid, NULL, 0); // Wait for child process to exit

 
cleanup:
    // Finalize output and clean up
    output[total_output] = '\0'; // Null-terminate output
    log_command(command, output, execution_timeout, timeout_occurred); // Log results
    close(client_socket); // Close client socket
    free(output); // Free output buffer
    return NULL;
}
 
// Main Function
// Sets up server socket and accepts client connections
int main() {
    int server_fd;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
 
    // Create TCP socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }
 
    // Configure socket address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces
    address.sin_port = htons(PORT); // Convert port to network byte order
 
    // Bind socket to address
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return 1;
    }
 
    // Listen for incoming connections
    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }
 
    printf("Server listening on port %d...\n", PORT);
 
    // Accept client connections in a loop
    while (1) {
        // Allocate memory for client socket
        int *client_socket = malloc(sizeof(int));
        if (!client_socket) {
            perror("Memory allocation failed");
            continue;
        }
 
        // Accept new connection
        *client_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (*client_socket < 0) {
            perror("Accept failed");
            free(client_socket);
            continue;
        }
 
        // Create thread to handle client
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, client_socket) != 0) {
            perror("Thread creation failed");
            close(*client_socket);
            free(client_socket);
            continue;
        }
        pthread_detach(tid); // Detach thread to avoid memory leak
    }
 
    close(server_fd); // Close server socket (unreachable in this loop)
    return 0;
}
 