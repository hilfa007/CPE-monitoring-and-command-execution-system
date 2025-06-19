// configuration file (threshold.conf) management

#include "config.h"
#include <stdio.h>
#include <string.h>

//load threshold values from a configuration file
// returns a Thresholds structure with either loaded values or defaults
Thresholds load_thresholds(const char *filename) {
    // thresholds structure with default values
    Thresholds t = {80.0, 75.0, 90.0, 86400.0, 5, 200}; 
    
    // attempt to open the configuration file in read mode
    FILE *fp = fopen(filename, "r");
    
    // Check if file opening failed
    if (!fp) {
        // log error and return default thresholds
        log_message("ERROR: Failed to open thresholds.conf, using defaults");
        return t;
    }

    // variables to store key-value pairs from the file
    char key[32];
    float value;
    
    // read file line by line until end of file
    while (fscanf(fp, "%[^=]=%f\n", key, &value) != EOF) {
        // validate float-based thresholds (memory, cpu, disk, uptime)
        // ensure they are positive
        if ((strcmp(key, "memory") == 0 || strcmp(key, "cpu") == 0 || 
             strcmp(key, "disk") == 0 || strcmp(key, "uptime") == 0) && 
            (value <= 0)) {
            // log warning for invalid values and skip to next iteration
            log_message("WARNING: Invalid threshold value %.1f for %s, ignoring", value, key);
            continue;
        }
        
        // validate integer-based thresholds (net_interfaces, processes)
        // ensure they are positive and whole numbers
        if (strcmp(key, "net_interfaces") == 0 || strcmp(key, "processes") == 0) {
            if (value <= 0 || value != (int)value) {
                log_message("WARNING: Invalid threshold value %.1f for %s, ignoring", value, key);
                continue;
            }
        }
        
        // assign values to corresponding fields in Thresholds structure
        if (strcmp(key, "memory") == 0) t.memory = value;
        else if (strcmp(key, "cpu") == 0) t.cpu = value;
        else if (strcmp(key, "disk") == 0) t.disk = value;
        else if (strcmp(key, "uptime") == 0) t.uptime = value;
        else if (strcmp(key, "net_interfaces") == 0) t.net_interfaces = (int)value;
        else if (strcmp(key, "processes") == 0) t.processes = (int)value;
    }
    
    fclose(fp);
    
    // log the loaded threshold values
    log_message("INFO: Loaded thresholds - memory: %.1f, cpu: %.1f, disk: %.1f, uptime: %.1f, net_interfaces: %d, processes: %d", 
                t.memory, t.cpu, t.disk, t.uptime, t.net_interfaces, t.processes);
    
    return t;
}