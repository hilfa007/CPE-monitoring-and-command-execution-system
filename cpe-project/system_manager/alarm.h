// Header guard to prevent multiple inclusions of this header file
#ifndef ALARM_H
#define ALARM_H

// Include necessary headers for Metrics, Thresholds, and logging functionality
#include "metrics.h"
#include "config.h"
#include "logger.h"

// Function declaration to check if metrics exceed specified thresholds
// Returns an integer indicating alarm status
int check_alarms(Metrics m, Thresholds t);

#endif // End of header guard