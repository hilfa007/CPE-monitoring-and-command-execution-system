#include "alarm.h"
#include "http_client.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Function to check system metrics against thresholds and trigger alarms if exceeded
int check_alarms(Metrics m, Thresholds t) {
    int alarm_triggered = 0; // Flag to indicate if any alarm was triggered
    char alarm_message[256]; // Buffer for alarm message
    char json_payload[512]; // Buffer for JSON payload to send to Cloud Manager

    // Check if memory usage exceeds threshold
    if (m.memory >= 0 && m.memory > t.memory) {
        // Format alarm message for memory usage
        snprintf(alarm_message, sizeof(alarm_message), "Memory usage (%.1f%%) exceeds threshold (%.1f%%)", m.memory, t.memory);
        log_message("ALARM: %s", alarm_message); // Log the alarm message
        // Format JSON payload with alarm details and metrics
        snprintf(json_payload, sizeof(json_payload), 
                 "{\"type\":\"alarm\",\"message\":\"%s\",\"metrics\":{\"memory\":%.1f,\"cpu\":%.1f,\"disk\":%.1f,\"uptime\":%.1f,\"net_interfaces\":%d,\"processes\":%d}}",
                 alarm_message, m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
        // Attempt to send alarm to Cloud Manager
        if (send_http("http://127.0.0.1:8082/alarm", json_payload)) {
            log_message("INFO: Alarm sent to Cloud Manager: %s", alarm_message); // Log success
        } else {
            log_message("ERROR: Failed to send alarm to Cloud Manager: %s", alarm_message); // Log failure
        }
        alarm_triggered = 1; // Set flag to indicate alarm was triggered
    }

    // Check if CPU usage exceeds threshold
    if (m.cpu >= 0 && m.cpu > t.cpu) {
        // Format alarm message for CPU usage
        snprintf(alarm_message, sizeof(alarm_message), "CPU usage (%.1f%%) exceeds threshold (%.1f%%)", m.cpu, t.cpu);
        log_message("ALARM: %s", alarm_message); // Log the alarm message
        // Format JSON payload with alarm details and metrics
        snprintf(json_payload, sizeof(json_payload), 
                 "{\"type\":\"alarm\",\"message\":\"%s\",\"metrics\":{\"memory\":%.1f,\"cpu\":%.1f,\"disk\":%.1f,\"uptime\":%.1f,\"net_interfaces\":%d,\"processes\":%d}}",
                 alarm_message, m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
        // Attempt to send alarm to Cloud Manager
        if (send_http("http://127.0.0.1:8082/alarm", json_payload)) {
            log_message("INFO: Alarm sent to Cloud Manager: %s", alarm_message); // Log success
        } else {
            log_message("ERROR: Failed to send alarm to Cloud Manager: %s", alarm_message); // Log failure
        }
        alarm_triggered = 1; // Set flag to indicate alarm was triggered
    }

    // Check if disk usage exceeds threshold
    if (m.disk >= 0 && m.disk > t.disk) {
        // Format alarm message for disk usage
        snprintf(alarm_message, sizeof(alarm_message), "Disk usage (%.1f%%) exceeds threshold (%.1f%%)", m.disk, t.disk);
        log_message("ALARM: %s", alarm_message); // Log the alarm message
        // Format JSON payload with alarm details and metrics
        snprintf(json_payload, sizeof(json_payload), 
                 "{\"type\":\"alarm\",\"message\":\"%s\",\"metrics\":{\"memory\":%.1f,\"cpu\":%.1f,\"disk\":%.1f,\"uptime\":%.1f,\"net_interfaces\":%d,\"processes\":%d}}",
                 alarm_message, m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
        // Attempt to send alarm to Cloud Manager
        if (send_http("http://127.0.0.1:8082/alarm", json_payload)) {
            log_message("INFO: Alarm sent to Cloud Manager: %s", alarm_message); // Log success
        } else {
            log_message("ERROR: Failed to send alarm to Cloud Manager: %s", alarm_message); // Log failure
        }
        alarm_triggered = 1; // Set flag to indicate alarm was triggered
    }

    // Check if uptime exceeds threshold
    if (m.uptime >= 0 && m.uptime > t.uptime) {
        // Format alarm message for uptime
        snprintf(alarm_message, sizeof(alarm_message), "Uptime (%.1f seconds) exceeds threshold (%.1f seconds)", m.uptime, t.uptime);
        log_message("ALARM: %s", alarm_message); // Log the alarm message
        // Format JSON payload with alarm details and metrics
        snprintf(json_payload, sizeof(json_payload), 
                 "{\"type\":\"alarm\",\"message\":\"%s\",\"metrics\":{\"memory\":%.1f,\"cpu\":%.1f,\"disk\":%.1f,\"uptime\":%.1f,\"net_interfaces\":%d,\"processes\":%d}}",
                 alarm_message, m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
        // Attempt to send alarm to Cloud Manager
        if (send_http("http://127.0.0.1:8082/alarm", json_payload)) {
            log_message("INFO: Alarm sent to Cloud Manager: %s", alarm_message); // Log success
        } else {
            log_message("ERROR: Failed to send alarm to Cloud Manager: %s", alarm_message); // Log failure
        }
        alarm_triggered = 1; // Set flag to indicate alarm was triggered
    }

    // Check if number of network interfaces exceeds threshold
    if (m.net_interfaces >= 0 && m.net_interfaces > t.net_interfaces) {
        // Format alarm message for network interfaces
        snprintf(alarm_message, sizeof(alarm_message), "Network interfaces (%d) exceeds threshold (%d)", m.net_interfaces, t.net_interfaces);
        log_message("ALARM: %s", alarm_message); // Log the alarm message
        // Format JSON payload with alarm details and metrics
        snprintf(json_payload, sizeof(json_payload), 
                 "{\"type\":\"alarm\",\"message\":\"%s\",\"metrics\":{\"memory\":%.1f,\"cpu\":%.1f,\"disk\":%.1f,\"uptime\":%.1f,\"net_interfaces\":%d,\"processes\":%d}}",
                 alarm_message, m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
        // Attempt to send alarm to Cloud Manager
        if (send_http("http://127.0.0.1:8082/alarm", json_payload)) {
            log_message("INFO: Alarm sent to Cloud Manager: %s", alarm_message); // Log success
        } else {
            log_message("ERROR: Failed to send alarm to Cloud Manager: %s", alarm_message); // Log failure
        }
        alarm_triggered = 1; // Set flag to indicate alarm was triggered
    }

    // Check if process count exceeds threshold
    if (m.processes >= 0 && m.processes > t.processes) {
        // Format alarm message for process count
        snprintf(alarm_message, sizeof(alarm_message), "Process count (%d) exceeds threshold (%d)", m.processes, t.processes);
        log_message("ALARM: %s", alarm_message); // Log the alarm message
        // Format JSON payload with alarm details and metrics
        snprintf(json_payload, sizeof(json_payload), 
                 "{\"type\":\"alarm\",\"message\":\"%s\",\"metrics\":{\"memory\":%.1f,\"cpu\":%.1f,\"disk\":%.1f,\"uptime\":%.1f,\"net_interfaces\":%d,\"processes\":%d}}",
                 alarm_message, m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
        // Attempt to send alarm to Cloud Manager
        if (send_http("http://127.0.0.1:8082/alarm", json_payload)) {
            log_message("INFO: Alarm sent to Cloud Manager: %s", alarm_message); // Log success
        } else {
            log_message("ERROR: Failed to send alarm to Cloud Manager: %s", alarm_message); // Log failure
        }
        alarm_triggered = 1; // Set flag to indicate alarm was triggered
    }

    // Return whether any alarm was triggered
    return alarm_triggered;
}