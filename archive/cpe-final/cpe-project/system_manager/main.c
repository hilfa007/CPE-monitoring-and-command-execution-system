#include "metrics.h"        // collect_metrics() and Metrics struct
#include "config.h"         // load_thresholds() and Thresholds struct
#include "alarm.h"          // check_alarms()
#include "device_agent_client.h"  // send_metrics_to_agent()
#include "http_client.h"    // send_http() (used in alarm.c)
#include "logger.h"         // log_message()
#include <unistd.h>         // sleep()
#include <sys/stat.h>       // stat() to check file changes
#include <time.h>           // time_t in stat
#include <stdio.h>

// global variable to hold threshold values (memory, cpu, etc.) loaded from config file.
// gets updated when thresholds.conf changes.
static Thresholds thresholds;

int main() {
    // load the initial thresholds from thresholds.conf at startup.
    // This sets up our limits for memory, cpu, disk, etc., which we’ll compare against metrics.
    thresholds = load_thresholds("config/thresholds.conf");

    // log that the program has started. This goes to whatever logging system logger.h defines
    // It’s just a way to confirm the program is running.
    log_message("System Manager started.");
    printf("System Manager started.\n");

    // set up variables to track changes to thresholds.conf.
    // config_stat will hold file metadata (like modification time), and last_modified
    // keeps the timestamp of the last time we checked the file.
    struct stat config_stat;
    time_t last_modified = 0;

    // check the initial state of thresholds.conf to get its modification time.
    // if the file exists, store its mtime in last_modified so we can detect future changes.
    // If stat fails (e.g., file doesn’t exist), we just keep last_modified as 0 and move on.
    if (stat("config/thresholds.conf", &config_stat) == 0) {
        last_modified = config_stat.st_mtime;
    }

    // how often we send metrics to the Device Agent and log them (every 10 seconds).
    // interval controls the frequency, and elapsed tracks how many seconds have passed.
    int interval = 10; // send and log metrics every 10 seconds
    int elapsed = 0;   // counter for elapsed seconds

    // main loop runs forever, monitoring the system in real-time.
    // each iteration takes about 1 second (due to sleep at the end).
    while (1) {
        // check if thresholds.conf has changed by getting its current metadata.
        // If stat fails (e.g., file deleted), log an error but keep running.
        if (stat("config/thresholds.conf", &config_stat) != 0) {
            log_message("ERROR: Failed to stat thresholds.conf");
            printf("ERROR: Failed to stat thresholds.conf");
        }
        // If the file’s modification time (st_mtime) is different from last_modified,
        // it means the file was edited or replaced. Reload thresholds and update last_modified.
        else if (config_stat.st_mtime != last_modified) {
            log_message("Detected config file change. Reloading...");
            thresholds = load_thresholds("config/thresholds.conf");
            last_modified = config_stat.st_mtime;
        }       

        // collect system metrics (memory, cpu, disk, etc.) using collect_metrics().
        // This reads from /proc, statvfs, etc., and returns a Metrics struct.
        Metrics m = collect_metrics();

        // validate the metrics to make sure they’re reasonable.
        // If any metric is negative (indicating an error in collection), log the issue
        // and skip this iteration. This prevents bad data from triggering alarms or being sent.
        if (m.memory < 0 || m.cpu < 0 || m.disk < 0 || m.uptime < 0 || m.net_interfaces < 0 || m.processes < 0) {
            log_message("ERROR: Invalid metrics collected - memory: %.1f, cpu: %.1f, disk: %.1f, uptime: %.1f, net_interfaces: %d, processes: %d",
                        m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
            sleep(1); // Wait 1 second before trying again
            continue; // Skip the rest of the loop
        }

        // Check if any metrics exceed thresholds (e.g., memory > 80%).
        // check_alarms() compares Metrics to Thresholds and, if breached, sends an HTTP POST
        // to the Cloud CLI (handled inside alarm.c). It returns 1 if an alarm was triggered.
        if (check_alarms(m, thresholds)) {
            // Log the alarm with full metrics for debugging.
            log_message("ALARM: Threshold breached! Metrics - memory: %.1f%%, cpu: %.1f%%, disk: %.1f%%, uptime: %.1f seconds, net_interfaces: %d, processes: %d",
                        m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
        }

        // Every 10 seconds, log metrics and send them to the Device Agent.
        // elapsed counts seconds, and when it hits interval (10), we perform these actions.
        if (elapsed >= interval) {
            // Log the current metrics to keep a record of system state.
            log_message("INFO: Collected metrics - memory: %.1f%%, cpu: %.1f%%, disk: %.1f%%, uptime: %.1f seconds, net_interfaces: %d, processes: %d",
                        m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);

            // Send metrics to the Device Agent via UNIX socket.
            // send_metrics_to_agent() formats metrics as a string, sends it, and waits for an ACK.
            // If it fails (no ACK or connection error), log an error. If it succeeds, log success.
            if (!send_metrics_to_agent(m)) {
                log_message("ERROR: Failed to send metrics, no ACK received.");
                printf("ERROR: Failed to send metrics \n");
            } else {
                log_message("INFO: Metrics sent and ACK received.");
            }
            elapsed = 0; // reset the counter to start counting to 10 again
        }

        // increment elapsed to track time since last metric send/log.
        elapsed++;
        // Sleep for 1 second to keep the loop running at 1second intervals.
        // This ensures real-time monitoring without overloading the CPU.
        sleep(1);
    }
    return 0;
}