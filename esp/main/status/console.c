#include "esp_console.h"

#include "status/console.h"
#include "status/status.h"

static int printStatus(int, char **) {
    Status currentStatus = statusGet();
    printf("Temperature: %.2f\n", currentStatus.temperature);
    printf("Free Heap: %lu\n", currentStatus.freeHeap);
    printf("Min Free Heap: %lu\n", currentStatus.minFreeHeap);
    printf("DB Size: %zu\n", currentStatus.dbSize);
    printf("Connection Status: %s\n", statusConnection2str(currentStatus.connection));
    printf("Uptime: %lld\n", currentStatus.uptime);
    int days = currentStatus.uptime / (1000000LL * 60 * 60 * 24);
    int hours = (currentStatus.uptime / (1000000LL * 60 * 60)) % 24;
    int minutes = (currentStatus.uptime / (1000000LL * 60)) % 60;
    int seconds = (currentStatus.uptime / 1000000LL) % 60;
    printf("Uptime: %d days, %d hours, %d minutes, %d seconds\n", days, hours, minutes, seconds);

    return 0;
}

esp_err_t statusRegisterCommands() {

    esp_console_cmd_t disconnectCommand = {
            .command = "status",
            .help = "Print current status",
            .argtable = NULL,
            .func = &printStatus,
    };
    esp_err_t err = esp_console_cmd_register(&disconnectCommand);

    return err;
}
