#include <stdio.h>
#include "metrics.h"

int main() {
    Metrics m = collect_metrics();

    printf("Memory Usage: %.2f%%\n", m.memory);
    printf("CPU Load: %.2f%%\n", m.cpu);
    printf("Uptime: %.2f seconds\n", m.uptime);
    printf("Disk Usage: %.2f%%\n", m.disk);
    printf("Network Interfaces: %d\n", m.net_interfaces);
    printf("Running Processes: %d\n", m.processes);

    return 0;
}
