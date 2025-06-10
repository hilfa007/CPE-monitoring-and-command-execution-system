#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define BUFFER_SIZE 1024
#define PORT 8080
#define DEFAULT_TIMEOUT 10
#define LONG_TIMEOUT 300

// List of interactive commands
const char *long_timeout_cmds[] = {"vim", "nano", "vi", "cat", "less", "more", "man", NULL};

// Check if the command is interactive
int is_long_timeout_command(const char *cmd) {
    for (int i = 0; long_timeout_cmds[i] != NULL; i++) {
        if (strstr(cmd, long_timeout_cmds[i]) == cmd) {
            return 1;
        }
    }
    return 0;
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

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    struct termios original;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        return 1;
    }

    printf("Enter a Linux command to run interactively: ");
    fgets(buffer, BUFFER_SIZE, stdin);
    buffer[strcspn(buffer, "\n")] = 0;

    // Preserve original command
    char original_cmd[BUFFER_SIZE];
    strncpy(original_cmd, buffer, BUFFER_SIZE);

    // Determine if it's a background process or long-timeout command
    int is_background = (strlen(original_cmd) > 0 && original_cmd[strlen(original_cmd) - 1] == '&');
    int timeout_seconds = DEFAULT_TIMEOUT;

    if (is_long_timeout_command(original_cmd) || is_background) {
        timeout_seconds = LONG_TIMEOUT;
    }

    send(sock, buffer, strlen(buffer), 0);

    set_raw_mode(&original);

    fd_set fds;
    struct timeval timeout;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(sock, &fds);
        int maxfd = (STDIN_FILENO > sock ? STDIN_FILENO : sock) + 1;

        timeout.tv_sec = timeout_seconds;
        timeout.tv_usec = 0;

        int activity = select(maxfd, &fds, NULL, NULL, &timeout);
        if (activity < 0) break;
        if (activity == 0) {
            printf("\n[Cloud manager] Response timeout (%d seconds).\n", timeout_seconds);
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &fds)) {
            int n = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (n <= 0) break;
            write(sock, buffer, n);
        }

        if (FD_ISSET(sock, &fds)) {
            int n = read(sock, buffer, sizeof(buffer));
            if (n <= 0) break;
            write(STDOUT_FILENO, buffer, n);
        }
    }

    restore_mode(&original);
    close(sock);
    return 0;
}
