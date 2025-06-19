// header file - config file
#ifndef CONFIG_H
#define CONFIG_H

#include "metrics.h"
#include "logger.h"

// Thresholds structure to hold configuration threshold values
typedef struct {
    float memory;         // memory usage percentage
    float cpu;            // CPU usage percentage
    float disk;           // disk usage percentage
    float uptime;         // system uptime in seconds
    int net_interfaces;   // number of active network interfaces
    int processes;        // for number of running processes
} Thresholds;

// load threshold values from a configuration file
// returns a Thresholds structure populated with values from the file
Thresholds load_thresholds(const char *filename);

#endif 