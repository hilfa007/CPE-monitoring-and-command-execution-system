#include "alarm.h"

int check_alarms(Metrics m, Thresholds t) {
    int alarm_triggered = 0;
    if (m.memory >= 0 && m.memory > t.memory) {
        log_message("ALARM: Memory usage (%.1f%%) exceeds threshold (%.1f%%)", m.memory, t.memory);
        alarm_triggered = 1;
    }
    if (m.cpu >= 0 && m.cpu > t.cpu) {
        log_message("ALARM: CPU usage (%.1f%%) exceeds threshold (%.1f%%)", m.cpu, t.cpu);
        alarm_triggered = 1;
    }
    if (m.disk >= 0 && m.disk > t.disk) {
        log_message("ALARM: Disk usage (%.1f%%) exceeds threshold (%.1f%%)", m.disk, t.disk);
        alarm_triggered = 1;
    }
    return alarm_triggered;
}
