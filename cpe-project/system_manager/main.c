#include "metrics.h"
#include "config.h"
#include "alarm.h"
#include "socket_client.h"
#include "http_client.h"
#include "logger.h"
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <time.h>

static Thresholds thresholds;
static volatile sig_atomic_t reload_config_flag = 0;

void handle_sighup(int sig) {
    reload_config_flag = 1;
}

int main() {
    // Load initial thresholds
    thresholds = load_thresholds("config/thresholds.conf");

    // Setup signal handler for dynamic config reload
    signal(SIGHUP, handle_sighup);

    // Initialize logger
    log_message("System Manager started.");

    struct stat config_stat;
    time_t last_modified = 0;

    if (stat("config/thresholds.conf", &config_stat) == 0) {
        last_modified = config_stat.st_mtime;
    }

    int interval = 10; // Log and send metrics every 10 seconds
    int elapsed = 0; // Track elapsed seconds

    while (1) {
        // Check for config file changes
        if (stat("config/thresholds.conf", &config_stat) != 0) {
            log_message("ERROR: Failed to stat thresholds.conf");
        } else if (config_stat.st_mtime != last_modified) {
            log_message("Detected config file change. Reloading...");
            thresholds = load_thresholds("config/thresholds.conf");
            last_modified = config_stat.st_mtime;
        }       

        // Collect system metrics
        Metrics m = collect_metrics();

        // Validate metrics to catch potential issues
        if (m.memory < 0 || m.cpu < 0 || m.disk < 0 || m.uptime < 0 || m.net_interfaces < 0 || m.processes < 0) {
            log_message("ERROR: Invalid metrics collected - memory: %.1f, cpu: %.1f, disk: %.1f, uptime: %.1f, net_interfaces: %d, processes: %d",
                        m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
            sleep(1); // Sleep 1 second for real-time monitoring
            continue;
        }

        // Check alarms in real-time (every 1 second)
        if (check_alarms(m, thresholds)) {
            log_message("ALARM: Threshold breached! Metrics - memory: %.1f%%, cpu: %.1f%%, disk: %.1f%%, uptime: %.1f seconds, net_interfaces: %d, processes: %d",
                        m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
            // HTTP POST to cloud handled in alarm.c
        }

        // Log metrics and send to agent every 10 seconds
        if (elapsed >= interval) {
            // Log collected metrics
            log_message("INFO: Collected metrics - memory: %.1f%%, cpu: %.1f%%, disk: %.1f%%, uptime: %.1f seconds, net_interfaces: %d, processes: %d",
                        m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);

            // Send metrics to agent
            if (!send_metrics_to_agent(m)) {
                log_message("ERROR: Failed to send metrics or no ACK received.");
            } else {
                log_message("INFO: Metrics sent and ACK received.");
            }
            elapsed = 0; // Reset counter
        }

        elapsed++; // Increment elapsed time
        sleep(1); // Sleep 1 second for real-time monitoring
    }

    return 0;
}