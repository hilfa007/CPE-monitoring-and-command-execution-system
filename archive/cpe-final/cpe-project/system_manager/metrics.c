// Metric collection

#include "metrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <time.h>

// ccalculate memory usage percentage
float get_memory_usage() {
    // Open /proc/meminfo to read memory information
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return -1; // Return -1 if file cannot be opened

    char line[256];
    long total = 0, free = 0;
    // Read lines from /proc/meminfo
    while (fgets(line, sizeof(line), fp)) {
        // extract total memory
        if (sscanf(line, "MemTotal: %ld kB", &total) == 1) continue;
        // extract available memory and stop reading
        if (sscanf(line, "MemAvailable: %ld kB", &free) == 1) break;
    }
    fclose(fp);
    // Calculate and return memory usage percentage
    return 100.0 * (1 - ((float)free / total));
}

// calculate CPU load percentage
float get_cpu_load() {
    // Static variables to store previous CPU usage values for delta calculation
    static long long prev_total = 0, prev_idle = 0;
    // Open /proc/stat to read CPU statistics
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return -1.0; // Return -1 if file cannot be opened

    long long user, nice, system, idle, iowait, irq, softirq;
    // Read CPU usage data from first line
    fscanf(fp, "cpu %lld %lld %lld %lld %lld %lld %lld", &user, &nice, &system, &idle, &iowait, &irq, &softirq);
    fclose(fp);

    // calculate total and idle CPU time
    long long total = user + nice + system + idle + iowait + irq + softirq;
    long long idle_total = idle + iowait;

    // if this is the first reading, store values and return 0
    if (prev_total == 0) {
        prev_total = total;
        prev_idle = idle_total;
        return 0.0; // First reading, no delta
    }

    // calculate differences from previous reading
    long long total_diff = total - prev_total;
    long long idle_diff = idle_total - prev_idle;
    prev_total = total;
    prev_idle = idle_total;

    // Avoid division by zero
    if (total_diff == 0) return 0.0;
    // calculate and return CPU load percentage
    return 100.0 * (total_diff - idle_diff) / total_diff;
}

// get system uptime in seconds
float get_uptime() {
    // Open /proc/uptime to read system uptime
    FILE *fp = fopen("/proc/uptime", "r");
    float uptime = 0.0;
    if (fp) {
        // Read uptime value
        fscanf(fp, "%f", &uptime);
        fclose(fp);
    }
    return uptime; // Return uptime in seconds
}

// calculate disk usage percentage for root filesystem
float get_disk_usage() {
    struct statvfs stat;
    // get filesystem statistics for root ("/")
    if (statvfs("/", &stat) != 0) return -1; // Return -1 if statvfs fails
    // calculate and return disk usage percentage
    return 100.0 * (1.0 - ((float)stat.f_bavail / stat.f_blocks));
}

// count active network interfaces
int get_network_interfaces() {
    struct ifaddrs *ifaddr, *ifa;
    int count = 0;
    // Get list of network interfaces
    if (getifaddrs(&ifaddr) == -1) return -1; // Return -1 if getifaddrs fails
    // Iterate through interfaces and count those with valid addresses
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
        if (ifa->ifa_addr) count++;
    // free allocated interface list
    freeifaddrs(ifaddr);
    return count; // Return number of active interfaces
}

// count running processes
int get_process_count() {
    // Open /proc directory to read process information
    DIR *dir = opendir("/proc");
    struct dirent *entry;
    int count = 0;
    // iterate through /proc directory entries
    while ((entry = readdir(dir)) != NULL) {
        // count directories with numeric names (process IDs)
        if (entry->d_type == DT_DIR && atoi(entry->d_name) > 0)
            count++;
    }
    closedir(dir);
    return count; // Return number of processes
}

// collect all system metrics
Metrics collect_metrics() {
    Metrics m;
    // populate Metrics structure with collected values
    m.memory = get_memory_usage();
    m.cpu = get_cpu_load();
    m.uptime = get_uptime();
    m.disk = get_disk_usage();
    m.net_interfaces = get_network_interfaces();
    m.processes = get_process_count();
    return m; // Return metrics
}