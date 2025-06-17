//device agent code
//libraries for I/O, memory management, strings, system calls, time, sockets, errors, file status and signals

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/stat.h>
#include <signal.h>

#define UNIX_SOCK_PATH "/tmp/device_agent.sock"    // Path for UNIX socket
#define CLOUD_HOST "127.0.0.1"                     //server IP address
#define CLOUD_PORT 8080                            //port for metrics
#define MAX_METRIC_SIZE 256
#define BUFFER_SIZE 360
#define LOG_FILE "metrics.log"
#define ACK_RETRIES 3
#define RETRY_DELAY 5
#define CONNECT_TIMEOUT 2 //// Timeout for TCP connection 
#define ACCEPT_TIMEOUT 5 // Timeout for accepting System Manager connections

// Circular Buffer Structure for storing the buffer metrics
typedef struct {
    char metrics[BUFFER_SIZE][MAX_METRIC_SIZE];
    int head;         //oldest metric index
    int tail;         // newsest metric index
    int count;
} CircularBuffer;

// Global Variables
int server_fd = -1;          //for system manager connection
int cloud_fd = -1;           //for cloud manager connection
CircularBuffer buffer;

// Function Prototypes for all functions
void init_buffer(CircularBuffer *buffer);
int buffer_metric(CircularBuffer *buffer, const char *metric);
int flush_buffer(CircularBuffer *buffer);
int connect_to_cloud_manager();
int forward_metric(const char *metric);
void log_metric(const char *metric);
int send_ack(int client_fd);
void cleanup();              //cleanuo resources
int check_socket_state();    //unix socket
void signal_handler(int sig);

// Signal handler for cleaning up resources and exits on critical signals
void signal_handler(int sig) {
    fprintf(stderr, "Caught signal %d, cleaning up\n", sig);
    cleanup();
    exit(1);
}

// Initialize Circular Buffer - setting up empty buffer
void init_buffer(CircularBuffer *buffer) {
    buffer->head = 0;
    buffer->tail = 0;
    buffer->count = 0;
    fprintf(stderr, "Circular buffer initialized, size: %d\n", BUFFER_SIZE);
}

// fuction to add a metric to Buffer during disconnection
int buffer_metric(CircularBuffer *buffer, const char *metric) {
    if (!metric) {
        fprintf(stderr, "Error: NULL metric in buffer_metric\n");  //checking if empty
        return -1;
    }
    if (buffer->count >= BUFFER_SIZE) { // if buffer is full, drop the oldest metric
        fprintf(stderr, "Buffer full, dropping oldest metric\n");
        buffer->head = (buffer->head + 1) % BUFFER_SIZE; //move head forward
        buffer->count--;
    }
    strncpy(buffer->metrics[buffer->tail], metric, MAX_METRIC_SIZE - 1);  //add metric at tail
    buffer->metrics[buffer->tail][MAX_METRIC_SIZE - 1] = '\0';
    buffer->tail = (buffer->tail + 1) % BUFFER_SIZE;
    buffer->count++;
    fprintf(stderr, "Buffered metric: %s, count: %d\n", metric, buffer->count);
    return 0;
}

// function to Flush one buffered metric to cloud manager
int flush_buffer(CircularBuffer *buffer) {
    if (buffer->count == 0) {                                 //check if empty
        fprintf(stderr, "Buffer empty, nothing to flush\n");
        return 0;
    }

    if (cloud_fd < 0) {            //check if connected to cloud manager
        fprintf(stderr, "No Cloud Manager connection, cannot flush\n");
        return -1;
    }

    fprintf(stderr, "Flushing metric: %s\n", buffer->metrics[buffer->head]);  //sendind the buffer to cloud manager
    ssize_t sent = send(cloud_fd, buffer->metrics[buffer->head], 
                        strlen(buffer->metrics[buffer->head]), 0);

    if (sent < 0) {                        //if send fails
        perror("Failed to flush metric");
        close(cloud_fd);
        cloud_fd = -1;
        return -1;
    }
    buffer->head = (buffer->head + 1) % BUFFER_SIZE;   //update buffer
    buffer->count--;
    fprintf(stderr, "Flushed one metric, remaining count: %d\n", buffer->count);
    return 0;
}

// function to Connect to Cloud Manager (blocking with timeout)
int connect_to_cloud_manager() {
    if (cloud_fd >= 0) {              //check if already connected
        fprintf(stderr, "Cloud Manager already connected\n");
        return 0;
    }

    fprintf(stderr, "Attempting to connect to Cloud Manager\n");
    cloud_fd = socket(AF_INET, SOCK_STREAM, 0);         //create socket
    if (cloud_fd < 0) {
        perror("Failed to create TCP socket");
        return -1;
    }

    // Set connect timeout
    struct timeval tv = { .tv_sec = CONNECT_TIMEOUT, .tv_usec = 0 };
    if (setsockopt(cloud_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Failed to set connect timeout");
        close(cloud_fd);
        cloud_fd = -1;
        return -1;
    }

    //setup cloud manager address
    struct sockaddr_in cloud_addr; 
    memset(&cloud_addr, 0, sizeof(cloud_addr));
    cloud_addr.sin_family = AF_INET;
    cloud_addr.sin_port = htons(CLOUD_PORT);
    if (inet_pton(AF_INET, CLOUD_HOST, &cloud_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid Cloud Manager address: %s\n", CLOUD_HOST);
        close(cloud_fd);
        cloud_fd = -1;
        return -1;
    }

    if (connect(cloud_fd, (struct sockaddr *)&cloud_addr, sizeof(cloud_addr)) < 0) {
        fprintf(stderr, "Connect failed: %s\n", strerror(errno));
        close(cloud_fd);
        cloud_fd = -1;
        return -1;
    }

    // Disable timeout after successful connection
    tv.tv_sec = 0;
    if (setsockopt(cloud_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Failed to disable connect timeout");
        close(cloud_fd);
        cloud_fd = -1;
        return -1;
    }

    // Set receive buffer size for Cloud Manager socket
    int bufsize = 65536;     //64 KB
    if (setsockopt(cloud_fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize)) < 0) {
        perror("Failed to set Cloud Manager socket receive buffer size");
        close(cloud_fd);
        cloud_fd = -1;
        return -1;
    }

    fprintf(stderr, "Connected to Cloud Manager at %s:%d\n", CLOUD_HOST, CLOUD_PORT);
    return 0;
}

// function to Forward metric to Cloud Manager
int forward_metric(const char *metric) {
    if (!metric) {                           //check if null
        fprintf(stderr, "Error: NULL metric in forward_metric\n");
        return -1;
    }
    if (cloud_fd < 0 && connect_to_cloud_manager() < 0) {                //attempt connection. if no connection buffer the metric
        fprintf(stderr, "No connection, buffering metric: %s\n", metric);
        return buffer_metric(&buffer, metric);
    }
    fprintf(stderr, "Forwarding metric: %s\n", metric);      //sending  metric
    ssize_t sent = send(cloud_fd, metric, strlen(metric), 0);
    if (sent < 0) {
        perror("Failed to forward metric");
        close(cloud_fd);
        cloud_fd = -1;
        fprintf(stderr, "Buffering metric due to send failure: %s\n", metric);
        return buffer_metric(&buffer, metric);
    }
    // Attempt to flush one buffered metric
    if (buffer.count > 0 && cloud_fd >= 0) {
        flush_buffer(&buffer);
    }
    return 0;
}

// function to Log metric to file with timestamp
void log_metric(const char *metric) {
    if (!metric) {
        fprintf(stderr, "Error: NULL metric in log_metric\n");
        return;
    }
    time_t now = time(NULL);     //get current timestamp
    char *time_str = ctime(&now);
    if (!time_str) {
        fprintf(stderr, "Failed to get timestamp\n");
        return;
    }
    time_str[strcspn(time_str, "\n")] = '\0';

    FILE *log = fopen(LOG_FILE, "a");       //open logfile in append mode
    if (log) {
        fprintf(log, "%s,%s\n", time_str, metric);      //write timestamp and metric
        fclose(log);
        fprintf(stderr, "Logged metric: %s\n", metric);
    } else {
        perror("Failed to open log file");
    }
}

// function to Send ACK to System Manager
int send_ack(int client_fd) {
    const char *ack = "ACK";
    int retries = ACK_RETRIES;   //retry count
    while (retries > 0) {
        ssize_t sent = send(client_fd, ack, strlen(ack), 0);
        if (sent == strlen(ack)) {
            fprintf(stderr, "Sent ACK to System Manager\n");
            return 0;
        }
        perror("Failed to send ACK");
        sleep(RETRY_DELAY);
        retries--;
    }
    fprintf(stderr, "Failed to send ACK after %d retries\n", ACK_RETRIES);
    return -1;
}

// function to Check UNIX socket state
int check_socket_state() {
    struct stat st;
    if (stat(UNIX_SOCK_PATH, &st) < 0) {               //check if socket file exists
        fprintf(stderr, "UNIX socket file missing: %s\n", UNIX_SOCK_PATH);
        return -1;
    }
    return 0;
}

// function to Cleanup resources - Closes sockets and removes UNIX socket file
void cleanup() {
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
        fprintf(stderr, "Closed UNIX socket\n");
    }
    if (cloud_fd >= 0) {
        close(cloud_fd);
        cloud_fd = -1;
        fprintf(stderr, "Closed Cloud Manager socket\n");
    }
    unlink(UNIX_SOCK_PATH);        // // Remove UNIX socket file
    fprintf(stderr, "Removed UNIX socket file: %s\n", UNIX_SOCK_PATH);
}

int main() {
    // Install signal handler
    signal(SIGSEGV, signal_handler);   // Handle segmentation faults
    signal(SIGTERM, signal_handler);   //// Handle termination signals
    signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE to prevent crashes on broken connections

    init_buffer(&buffer);  //initialize circular buffer

    // Create UNIX domain socket
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Failed to create UNIX socket");
        exit(1);
    }

    struct sockaddr_un addr;     //setup socket address
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, UNIX_SOCK_PATH, sizeof(addr.sun_path) - 1);
    unlink(UNIX_SOCK_PATH); // Remove stale socket

    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Failed to bind UNIX socket");
        cleanup();
        exit(1);
    }

    // New: Set socket permissions to allow System Manager access 
    if (chmod(UNIX_SOCK_PATH, 0666) < 0) {
        perror("Failed to set socket permissions");
        cleanup();
        exit(1);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Failed to listen on UNIX socket");
        cleanup();
        exit(1);
    }

    // New: Set accept timeout
    struct timeval tv = { .tv_sec = ACCEPT_TIMEOUT, .tv_usec = 0 };
    if (setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Failed to set accept timeout");
        cleanup();
        exit(1);
    }

    // set socket buffer size for receiving(unix socket)
    int bufsize = 65536;
    if (setsockopt(server_fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize)) < 0) {
        perror("Failed to set socket buffer size");
        cleanup();
        exit(1);
    }
    fprintf(stderr, "Socket buffer size set to %d\n", bufsize);

    printf("Device Agent running, listening on %s\n", UNIX_SOCK_PATH);

     // Main loop: Accept and process System Manager connections
    while (1) {
        // Check  unix socket state
        if (check_socket_state() < 0) {
            fprintf(stderr, "Socket error, restarting UNIX socket\n");
            cleanup();
            server_fd = socket(AF_UNIX, SOCK_STREAM, 0);    // Recreate UNIX socket
            if (server_fd < 0) {
                perror("Failed to recreate UNIX socket");
                exit(1);
            }
            memset(&addr, 0, sizeof(addr)); //rebind socket
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, UNIX_SOCK_PATH, sizeof(addr.sun_path) - 1);
            if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                perror("Failed to bind UNIX socket");
                cleanup();
                exit(1);
            }
            if (chmod(UNIX_SOCK_PATH, 0666) < 0) { // New: Reapply permissions
                perror("Failed to set socket permissions");
                cleanup();
                exit(1);
            }
            if (listen(server_fd, 10) < 0) {
                perror("Failed to listen on UNIX socket");
                cleanup();
                exit(1);
            }
            if (setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
                perror("Failed to set accept timeout");
                cleanup();
                exit(1);
            }
            if (setsockopt(server_fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize)) < 0) {
                perror("Failed to set socket buffer size");
                cleanup();
                exit(1);
            }
            fprintf(stderr, "UNIX socket recreated\n");
        }
        

        //accept system manager connection
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr, "Accept timeout, continuing\n");
                continue;
            }
            perror("Failed to accept connection");
            continue;
        }
        fprintf(stderr, "Accepted new System Manager connection\n");

        // receive metric
        char metric[MAX_METRIC_SIZE];
        ssize_t len = recv(client_fd, metric, MAX_METRIC_SIZE - 1, 0);
        if (len <= 0) {
            if (len == 0) {
                fprintf(stderr, "System Manager closed connection\n");
            } else {
                perror("Failed to receive metric");
            }
            close(client_fd);
            continue;       //accept next connection
        }
        metric[len] = '\0';
        // New: Validate metric format (basic check)
        if (strnlen(metric, MAX_METRIC_SIZE) == 0 || strchr(metric, '=') == NULL) {
            fprintf(stderr, "Invalid metric received, discarding\n");
            close(client_fd);
            continue;
        }
        fprintf(stderr, "Received metric: %s\n", metric);

        log_metric(metric);
        if (send_ack(client_fd) < 0) {
            fprintf(stderr, "Failed to send ACK after retries\n");
        }
        forward_metric(metric);
        close(client_fd);
        fprintf(stderr, "Closed System Manager connection\n");
    }

    cleanup();
    return 0;
}
