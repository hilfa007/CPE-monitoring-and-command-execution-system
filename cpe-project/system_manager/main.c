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

// Static global variables for thresholds and configuration reload flag
static Thresholds thresholds; // Stores threshold values loaded from config file
static volatile sig_atomic_t reload_config_flag = 0; // Flag to trigger config reload on SIGHUP

// Signal handler for SIGHUP to initiate configuration reload
void handle_sighup(int sig) {
    reload_config_flag = 1;
}

// Main function for the system manager
int main() {
    // Load initial thresholds from configuration file
    thresholds = load_thresholds("config/thresholds.conf");

    // Set up signal handler for SIGHUP to allow dynamic config reload
    signal(SIGHUP, handle_sighup);

    // Log startup message
    log_message("System Manager started.");

    // Structure to store file status for config file
    struct stat config_stat;
    time_t last_modified = 0; // Track last modification time of config file

    // Get initial modification time of config file
    if (stat("config/thresholds.conf", &config_stat) == 0) {
        last_modified = config_stat.st_mtime;
    }

    // Main loop for continuous monitoring
    while (1) {
        // Check for changes in config file
        if (stat("config/thresholds.conf", &config_stat) != 0) {
            // Log error if unable to stat config file
            log_message("ERROR: Failed to stat thresholds.conf");
        } else if (config_stat.st_mtime != last_modified) {
            // Reload thresholds if config file has changed
            log_message("Detected config file change. Reloading...");
            thresholds = load_thresholds("config/thresholds.conf");
            last_modified = config_stat.st_mtime;
        }       

        // Collect system metrics
        Metrics m = collect_metrics();

        // Validate collected metrics to ensure they are non-negative
        if (m.memory < 0 || m.cpu < 0 || m.disk < 0 || m.uptime < 0 || m.net_interfaces < 0 || m.processes < 0) {
            // Log error for invalid metrics and wait before retrying
            log_message("ERROR: Invalid metrics collected - memory: %.1f, cpu: %.1f, disk: %.1f, uptime: %.1f, net_interfaces: %d, processes: %d",
                        m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
            sleep(10);
            continue;
        }

        // Log collected metrics for informational purposes
        log_message("INFO: Collected metrics - memory: %.1f%%, cpu: %.1f%%, disk: %.1f%%, uptime: %.1f seconds, net_interfaces: %d, processes: %d",
                    m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);

        // Check if metrics exceed thresholds and trigger alarm if necessary
        if (check_alarms(m, thresholds)) {
            // Log alarm for threshold breach
            log_message("ALARM: Threshold breached! Metrics - memory: %.1f%%, cpu: %.1f%%, disk: %.1f%%, uptime: %.1f seconds, net_interfaces: %d, processes: %d",
                        m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
            // Optional: Placeholder for sending HTTP POST to cloud service
        } else {
            // Log that metrics are within acceptable thresholds
            log_message("INFO: Metrics within thresholds - memory: %.1f%%, cpu: %.1f%%, disk: %.1f%%, uptime: %.1f seconds, net_interfaces: %d, processes: %d",
                        m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
        }

        // Send metrics to agent and check for acknowledgment
        if (!send_metrics_to_agent(m)) {
            // Log error if sending metrics fails or no ACK received
            log_message("ERROR: Failed to send metrics or no ACK received.");
        } else {
            // Log success for metrics sent and ACK received
            log_message("INFO: Metrics sent and ACK received.");
        }

        // Wait for 10 seconds before next iteration
        sleep(10);
    }

    // Return 0 (though loop is infinite and this is unreachable)
    return 0;
}