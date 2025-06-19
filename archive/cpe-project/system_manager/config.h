// Header guard to prevent multiple inclusions of this header file
#ifndef CONFIG_H
#define CONFIG_H

// Include necessary headers for Metrics and logging functionality
#include "metrics.h"
#include "logger.h"

// Define Thresholds structure to hold configuration threshold values
typedef struct {
    float memory;         // Threshold for memory usage percentage
    float cpu;            // Threshold for CPU usage percentage
    float disk;           // Threshold for disk usage percentage
    float uptime;         // Threshold for system uptime in seconds
    int net_interfaces;   // Threshold for number of active network interfaces
    int processes;        // Threshold for number of running processes
} Thresholds;

// Function declaration to load threshold values from a configuration file
// Returns a Thresholds structure populated with values from the file
Thresholds load_thresholds(const char *filename);

#endif // End of header guard