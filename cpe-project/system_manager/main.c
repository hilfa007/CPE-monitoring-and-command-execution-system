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

    while (1) {
        if (stat("config/thresholds.conf", &config_stat) != 0) {
            log_message("ERROR: Failed to stat thresholds.conf");
        } else if (config_stat.st_mtime != last_modified) {
            log_message("Detected config file change. Reloading...");
            thresholds = load_thresholds("config/thresholds.conf");
            last_modified = config_stat.st_mtime;
        }       

        Metrics m = collect_metrics();

        // Validate metrics to catch potential issues
        if (m.memory < 0 || m.cpu < 0 || m.disk < 0 || m.uptime < 0 || m.net_interfaces < 0 || m.processes < 0) {
            log_message("ERROR: Invalid metrics collected - memory: %.1f, cpu: %.1f, disk: %.1f, uptime: %.1f, net_interfaces: %d, processes: %d",
                        m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
            sleep(10);
            continue;
        }

        // Log collected metrics before sending to agent
        log_message("INFO: Collected metrics - memory: %.1f%%, cpu: %.1f%%, disk: %.1f%%, uptime: %.1f seconds, net_interfaces: %d, processes: %d",
                    m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);

        if (check_alarms(m, thresholds)) {
            log_message("ALARM: Threshold breached! Metrics - memory: %.1f%%, cpu: %.1f%%, disk: %.1f%%, uptime: %.1f seconds, net_interfaces: %d, processes: %d",
                        m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
            // Optional: send HTTP POST to cloud here
        } else {
            log_message("INFO: Metrics within thresholds - memory: %.1f%%, cpu: %.1f%%, disk: %.1f%%, uptime: %.1f seconds, net_interfaces: %d, processes: %d",
                        m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
        }

        if (!send_metrics_to_agent(m)) {
            log_message("ERROR: Failed to send metrics or no ACK received.");
        } else {
            log_message("INFO: Metrics sent and ACK received.");
        }

        sleep(10);
    }

    return 0;
}