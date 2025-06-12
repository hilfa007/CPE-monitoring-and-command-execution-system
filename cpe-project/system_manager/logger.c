#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

void log_message(const char *format, ...) {
    FILE *fp = fopen("logs/system_manager.log", "a");
    if (!fp) {
        fprintf(stderr, "ERROR: Failed to open log file\n");
        return;
    }

    // Get current time
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        fprintf(fp, "[ERROR: Failed to get time] ");
    } else {
        char time_buf[64]; // Larger buffer for safety
        struct tm *tm_info = localtime(&now);
        if (tm_info) {
            strftime(time_buf, sizeof(time_buf), "[%a %b %d %H:%M:%S %Y]", tm_info);
            fprintf(fp, "%s ", time_buf);
        } else {
            fprintf(fp, "[ERROR: Failed to format time] ");
        }
    }

    // Handle variable arguments
    va_list args;
    va_start(args, format);
    vfprintf(fp, format, args);
    va_end(args);

    // Add newline and close file
    fprintf(fp, "\n");
    fclose(fp);
}