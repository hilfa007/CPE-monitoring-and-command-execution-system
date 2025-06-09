#include "config.h"
#include <stdio.h>
#include <string.h>

Thresholds load_thresholds(const char *filename) {
    Thresholds t = {80.0, 75.0, 90.0}; // defaults
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        log_message("ERROR: Failed to open thresholds.conf, using defaults");
        return t;
    }

    char key[32];
    float value;
    while (fscanf(fp, "%[^=]=%f\n", key, &value) != EOF) {
        if (value <= 0 || value > 100) {
            log_message("WARNING: Invalid threshold value %.1f for %s, ignoring", value, key);
            continue;
        }
        if (strcmp(key, "memory") == 0) t.memory = value;
        else if (strcmp(key, "cpu") == 0) t.cpu = value;
        else if (strcmp(key, "disk") == 0) t.disk = value;
    }
    fclose(fp);
    log_message("INFO: Loaded thresholds - memory: %.1f, cpu: %.1f, disk: %.1f", t.memory, t.cpu, t.disk);
    return t;
}
