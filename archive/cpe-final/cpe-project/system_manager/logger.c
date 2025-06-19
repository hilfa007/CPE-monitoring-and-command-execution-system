// log file manager

#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

// log messages to a file with a timestamp
void log_message(const char *format, ...) {
    FILE *fp = fopen("logs/system_manager.log", "a");
    if (!fp) {
        // print error to stderr if file opening fails
        fprintf(stderr, "ERROR: Failed to open log file\n");
        return;
    }

    // get current time
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        // log error if time retrieval fails
        fprintf(fp, "[ERROR: Failed to get time] ");
    } else {
        // Buffer to store formatted time
        char time_buf[64]; // Larger buffer for safety
        // Convert time to local time
        struct tm *tm_info = localtime(&now);
        if (tm_info) {
            // Format time as "[Day Mon DD HH:MM:SS YYYY]"
            strftime(time_buf, sizeof(time_buf), "[%a %b %d %H:%M:%S %Y]", tm_info);
            fprintf(fp, "%s ", time_buf);
        } else {
            fprintf(fp, "[ERROR: Failed to format time] "); // log error
        }
    }

    // variable arguments for flexible message formatting
    va_list args;
    va_start(args, format); // Initialize variable argument list
    vfprintf(fp, format, args); // Write formatted message to log file
    va_end(args); // Clean up variable argument list

    fprintf(fp, "\n");
    fclose(fp);
}