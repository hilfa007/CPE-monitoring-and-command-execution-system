#include "config.h"
#include <stdio.h>
#include <string.h>

Thresholds load_thresholds(const char *filename) {
    Thresholds t = {80.0, 75.0, 90.0, 86400.0, 5, 200}; // defaults
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        log_message("ERROR: Failed to open thresholds.conf, using defaults");
        return t;
    }

    char key[32];
    float value;
    while (fscanf(fp, "%[^=]=%f\n", key, &value) != EOF) {
        if ((strcmp(key, "memory") == 0 || strcmp(key, "cpu") == 0 || strcmp(key, "disk") == 0 || strcmp(key, "uptime") == 0) && (value <= 0)) {
            log_message("WARNING: Invalid threshold value %.1f for %s, ignoring", value, key);
            continue;
        }
        if (strcmp(key, "net_interfaces") == 0 || strcmp(key, "processes") == 0) {
            if (value <= 0 || value != (int)value) {
                log_message("WARNING: Invalid threshold value %.1f for %s, ignoring", value, key);
                continue;
            }
        }
        if (strcmp(key, "memory") == 0) t.memory = value;
        else if (strcmp(key, "cpu") == 0) t.cpu = value;
        else if (strcmp(key, "disk") == 0) t.disk = value;
        else if (strcmp(key, "uptime") == 0) t.uptime = value;
        else if (strcmp(key, "net_interfaces") == 0) t.net_interfaces = (int)value;
        else if (strcmp(key, "processes") == 0) t.processes = (int)value;
    }
    fclose(fp);
    log_message("INFO: Loaded thresholds - memory: %.1f, cpu: %.1f, disk: %.1f, uptime: %.1f, net_interfaces: %d, processes: %d", 
                t.memory, t.cpu, t.disk, t.uptime, t.net_interfaces, t.processes);
    return t;
}