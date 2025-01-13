#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"

#include "wifi/console.h"
#include "wifi/wifi.h"
#include "storage_wrapper.h"

#include "config.h"

static struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} connectArgs;

static int disconnect(int argc, char **argv) {
    wifiDisconnect();
    return 0;
}

static int connect(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&connectArgs);
    if (nerrors != 0) {
        arg_print_errors(stderr, connectArgs.end, argv[0]);
        return 1;
    }

    const char *ssid = connectArgs.ssid->sval[0];
    const char *password;
    if (connectArgs.password->count >= 1)
        password = connectArgs.password->sval[0];
    else
        password = NULL;

    if (strlen(ssid) >= 31) {
        printf("SSID must be less than %d characters long.\n", 31);
        return 1;
    }
    if (password != NULL && strlen(password) >= 63) {
        printf("Password must be less than %d characters long.\n", 63);
        return 1;
    }

    storageSetString("ssid", ssid);

    if (password != NULL)
        storageSetString("password", password);
    else
        storageDelete("password");

    printf("Saved data. Restarting in 1s...\n");

    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_restart();
    return 0;
}

esp_err_t wifiRegisterCommands() {
    esp_console_cmd_t disconnectCommand = {
        .command = "wifi-disconnect",
        .help = "Disconnects from WiFi",
        .argtable = NULL,
        .func = &disconnect,
    };
    esp_err_t err = esp_console_cmd_register(&disconnectCommand);
    if (err != ESP_OK)
        return err;

    connectArgs.ssid = arg_str1("s", "ssid", "<string>", "SSID of the network to connect to");
    connectArgs.password = arg_str0("p", "pass", "<string>", "Password of the network to connect to");
    connectArgs.end = arg_end(2);

    esp_console_cmd_t connectCommand = {
        .command = "wifi-connect",
        .help = "Connects to a WiFi network",
        .argtable = &connectArgs,
        .func = &connect,
    };
    return esp_console_cmd_register(&connectCommand);
}