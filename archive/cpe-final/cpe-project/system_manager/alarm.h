// header file for alarm.c

#ifndef ALARM_H
#define ALARM_H

#include "metrics.h"
#include "config.h"
#include "logger.h"

// check if metrics exceed specified thresholds
// returns an integer indicating alert status
int check_alarms(Metrics m, Thresholds t);

#endif 