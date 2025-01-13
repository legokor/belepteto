#include "esp_console.h"

#include "console.h"
#include "websocket/console.h"
#include "wifi/console.h"
#include "db/console.h"
#include "storage_wrapper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_system.h"

static int doReboot(int argc, char **argv) {
    printf("Rebooting in 5 seconds\n");
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    esp_restart();
    return 0;
}

static esp_err_t commandsRegisterRebootCommand() {
    esp_console_cmd_t command = {
        .command = "reboot",
        .help = "Reboots the device.",
        .argtable = NULL,
        .func = &doReboot,
    };
    return esp_console_cmd_register(&command);
}

void consoleInit() {
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "ESP $";
    repl_config.max_cmdline_length = 1024;

    esp_console_register_help_command();
    commandsRegisterRebootCommand();
    storageRegisterCommands();
    dbRegisterCommands();
    websocketRegisterCommands();
    wifiRegisterCommands();

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_console_new_repl_uart(&hw_config, &repl_config, &repl);

#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &repl);

#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl);

#else

#error Unsupported console type

#endif

    esp_console_start_repl(repl);
}
