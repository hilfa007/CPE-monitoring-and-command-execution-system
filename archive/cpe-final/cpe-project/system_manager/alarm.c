#include "alarm.h"
#include "http_client.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// check system metrics against thresholds and trigger alarms if exceeded
int check_alarms(Metrics m, Thresholds t) {
    int alarm_triggered = 0; // indicate if any alarm was triggered
    char alarm_message[256]; 
    char json_payload[512]; 

    // check if memory usage exceeds threshold
    if (m.memory >= 0 && m.memory > t.memory) {
        // format alarm message  for memory usage
        snprintf(alarm_message, sizeof(alarm_message), "Memory usage (%.1f%%) exceeds threshold (%.1f%%)", m.memory, t.memory);
        log_message("ALARM: %s", alarm_message); // Log 
        // JSON payload with alarm details and metrics
        snprintf(json_payload, sizeof(json_payload), 
                 "{\"type\":\"alarm\",\"message\":\"%s\",\"metrics\":{\"memory\":%.1f,\"cpu\":%.1f,\"disk\":%.1f,\"uptime\":%.1f,\"net_interfaces\":%d,\"processes\":%d}}",
                 alarm_message, m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
        // attempt to send alarm to cvloud Manager
        if (send_http("http://127.0.0.1:8082/alarm", json_payload)) {
            log_message("INFO: Alarm sent to Cloud Manager: %s", alarm_message); // log success
        } else {
            log_message("ERROR: Failed to send alarm to Cloud Manager: %s", alarm_message); // log failure
        }
        alarm_triggered = 1; // Set flag to indicate alarm was triggered
    }

    // check if CPU usage exceeds threshold
    if (m.cpu >= 0 && m.cpu > t.cpu) {
        
        snprintf(alarm_message, sizeof(alarm_message), "CPU usage (%.1f%%) exceeds threshold (%.1f%%)", m.cpu, t.cpu);
        log_message("ALARM: %s", alarm_message); 
        // Format JSON payload with alarm details and metrics
        snprintf(json_payload, sizeof(json_payload), 
                 "{\"type\":\"alarm\",\"message\":\"%s\",\"metrics\":{\"memory\":%.1f,\"cpu\":%.1f,\"disk\":%.1f,\"uptime\":%.1f,\"net_interfaces\":%d,\"processes\":%d}}",
                 alarm_message, m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
        // Attempt to send alarm to Cloud Manager
        if (send_http("http://127.0.0.1:8082/alarm", json_payload)) {
            log_message("INFO: Alarm sent to Cloud Manager: %s", alarm_message); // success
        } else {
            log_message("ERROR: Failed to send alarm to Cloud Manager: %s", alarm_message); // failure
        }
        alarm_triggered = 1; 
    }

    // check if disk usage exceeds threshold
    if (m.disk >= 0 && m.disk > t.disk) {
        // alarm message for disk usage
        snprintf(alarm_message, sizeof(alarm_message), "Disk usage (%.1f%%) exceeds threshold (%.1f%%)", m.disk, t.disk);
        log_message("ALARM: %s", alarm_message); 
        snprintf(json_payload, sizeof(json_payload), 
                 "{\"type\":\"alarm\",\"message\":\"%s\",\"metrics\":{\"memory\":%.1f,\"cpu\":%.1f,\"disk\":%.1f,\"uptime\":%.1f,\"net_interfaces\":%d,\"processes\":%d}}",
                 alarm_message, m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
        // attempt to send alarm to Cloud Manager
        if (send_http("http://127.0.0.1:8082/alarm", json_payload)) {
            log_message("INFO: Alarm sent to Cloud Manager: %s", alarm_message); //success log
        } else {
            log_message("ERROR: Failed to send alarm to Cloud Manager: %s", alarm_message); // failure log
        }
        alarm_triggered = 1; 
    }

    // check if uptime exceeds threshold
    if (m.uptime >= 0 && m.uptime > t.uptime) {
        // alarm message for uptime
        snprintf(alarm_message, sizeof(alarm_message), "Uptime (%.1f seconds) exceeds threshold (%.1f seconds)", m.uptime, t.uptime);
        log_message("ALARM: %s", alarm_message); 
        snprintf(json_payload, sizeof(json_payload), 
                 "{\"type\":\"alarm\",\"message\":\"%s\",\"metrics\":{\"memory\":%.1f,\"cpu\":%.1f,\"disk\":%.1f,\"uptime\":%.1f,\"net_interfaces\":%d,\"processes\":%d}}",
                 alarm_message, m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
        // attempt to send alarm to Cloud Manager
        if (send_http("http://127.0.0.1:8082/alarm", json_payload)) {
            log_message("INFO: Alarm sent to Cloud Manager: %s", alarm_message); // Success
        } else {
            log_message("ERROR: Failed to send alarm to Cloud Manager: %s", alarm_message); // Failure
        }
        alarm_triggered = 1; 
    }

    // check if number of network interfaces exceeds threshold
    if (m.net_interfaces >= 0 && m.net_interfaces > t.net_interfaces) {
        // alarm message for network interfaces
        snprintf(alarm_message, sizeof(alarm_message), "Network interfaces (%d) exceeds threshold (%d)", m.net_interfaces, t.net_interfaces);
        log_message("ALARM: %s", alarm_message);
        snprintf(json_payload, sizeof(json_payload), 
                 "{\"type\":\"alarm\",\"message\":\"%s\",\"metrics\":{\"memory\":%.1f,\"cpu\":%.1f,\"disk\":%.1f,\"uptime\":%.1f,\"net_interfaces\":%d,\"processes\":%d}}",
                 alarm_message, m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
        //attempt to send alarm to Cloud Manager
        if (send_http("http://127.0.0.1:8082/alarm", json_payload)) {
            log_message("INFO: Alarm sent to Cloud Manager: %s", alarm_message); 
        } else {
            log_message("ERROR: Failed to send alarm to Cloud Manager: %s", alarm_message); 
        }
        alarm_triggered = 1; // Set flag to indicate alarm was triggered
    }

    // check if process count exceeds threshold
    if (m.processes >= 0 && m.processes > t.processes) {
        // alarm message for process count
        snprintf(alarm_message, sizeof(alarm_message), "Process count (%d) exceeds threshold (%d)", m.processes, t.processes);
        log_message("ALARM: %s", alarm_message); // Log the alarm message
        snprintf(json_payload, sizeof(json_payload), 
                 "{\"type\":\"alarm\",\"message\":\"%s\",\"metrics\":{\"memory\":%.1f,\"cpu\":%.1f,\"disk\":%.1f,\"uptime\":%.1f,\"net_interfaces\":%d,\"processes\":%d}}",
                 alarm_message, m.memory, m.cpu, m.disk, m.uptime, m.net_interfaces, m.processes);
        // attempt to send alarm to Cloud Manager
        if (send_http("http://127.0.0.1:8082/alarm", json_payload)) {
            log_message("INFO: Alarm sent to Cloud Manager: %s", alarm_message); // log success
        } else {
            log_message("ERROR: Failed to send alarm to Cloud Manager: %s", alarm_message); // log failure
        }
        alarm_triggered = 1; // Set flag to indicate alarm was triggered
    }

    return alarm_triggered; // whether any alarm was triggered
}