#ifndef CONFIG_H
#define CONFIG_H

#include "metrics.h"
#include "logger.h"

typedef struct {
    float memory;
    float cpu;
    float disk;
} Thresholds;

Thresholds load_thresholds(const char *filename);

#endif
