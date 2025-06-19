// Metrics collection header file

#ifndef METRICS_H
#define METRICS_H

typedef struct {
    float memory;
    float cpu;
    float uptime;
    float disk;
    int net_interfaces;
    int processes;
} Metrics; // Structure to store collected metrices

Metrics collect_metrics();

#endif

