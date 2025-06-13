#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

// Function to log messages to a file with a timestamp
void log_message(const char *format, ...) {
    // Open the log file in append mode
    FILE *fp = fopen("logs/system_manager.log", "a");
    if (!fp) {
        // Print error to stderr if file opening fails
        fprintf(stderr, "ERROR: Failed to open log file\n");
        return;
    }

    // Get current time
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        // Log error if time retrieval fails
        fprintf(fp, "[ERROR: Failed to get time] ");
    } else {
        // Buffer to store formatted time
        char time_buf[64]; // Larger buffer for safety
        // Convert time to local time
        struct tm *tm_info = localtime(&now);
        if (tm_info) {
            // Format time as "[Day Mon DD HH:MM:SS YYYY]"
            strftime(time_buf, sizeof(time_buf), "[%a %b %d %H:%M:%S %Y]", tm_info);
            // Write formatted time to log file
            fprintf(fp, "%s ", time_buf);
        } else {
            // Log error if time formatting fails
            fprintf(fp, "[ERROR: Failed to format time] ");
        }
    }

    // Handle variable arguments for flexible message formatting
    va_list args;
    va_start(args, format); // Initialize variable argument list
    vfprintf(fp, format, args); // Write formatted message to log file
    va_end(args); // Clean up variable argument list

    // Add newline to log entry and close the file
    fprintf(fp, "\n");
    fclose(fp);
}