#ifndef CONFIG_H
#define CONFIG_H

#include "metrics.h"
#include "logger.h"

typedef struct {
    float memory;
    float cpu;
    float disk;
    float uptime;
    int net_interfaces;
    int processes;
} Thresholds;

Thresholds load_thresholds(const char *filename);

#endif