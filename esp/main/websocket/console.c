#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"

#include "websocket/console.h"
#include "websocket/websocket.h"
#include "storage_wrapper.h"

static struct {
    struct arg_str *uri;
    struct arg_str *user;
    struct arg_str *pass;
    struct arg_end *end;
} setArgs;

static int destroy(int argc, char **argv) {
    websocketDestroy();
    return 0;
}

static int settingsSet(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&setArgs);
    if (nerrors != 0) {
        arg_print_errors(stderr, setArgs.end, argv[0]);
        return 1;
    }

    const char *uri = setArgs.uri->sval[0];
    const char *user = setArgs.user->count > 0 ? setArgs.user->sval[0] : NULL;
    const char *pass = setArgs.pass->count > 0 ? setArgs.pass->sval[0] : NULL;

    // Limit the length of text fields to 63 characters
    if (strlen(uri) > 63) {
        printf("Error: URI length exceeds the limit of 63 characters\n");
        return 1;
    }
    if (user != NULL && strlen(user) > 63) {
        printf("Error: User length exceeds the limit of 63 characters\n");
        return 3;
    }
    if (pass != NULL && strlen(pass) > 63) {
        printf("Error: Password length exceeds the limit of 63 characters\n");
        return 4;
    }

    storageSetString("wsURI", uri);
    if (user != NULL) {
        storageSetString("wsUser", user);
    } else {
        storageDelete("wsUser");
    }
    if (pass != NULL) {
        storageSetString("wsPass", pass);
    } else {
        storageDelete("wsPass");
    }

    printf("Saved data. Restarting in 1s...\n");

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    esp_restart();
    return 0;
}

esp_err_t websocketRegisterCommands() {
    esp_console_cmd_t command = {
        .command = "websocket-disconnect",
        .help = "Destroys the current websocket connection",
        .argtable = NULL,
        .func = &destroy,
    };

    esp_err_t err = esp_console_cmd_register(&command);
    if (err != ESP_OK)
        return err;

    esp_console_cmd_t setCommand = {
        .command = "websocket-settings-set",
        .help = "Set the websocket settings",
        .hint = NULL,
        .func = &settingsSet,
        .argtable = &setArgs,
    };

    setArgs.uri = arg_str1("u", "uri", "<string>", "Websocket server URL");
    setArgs.user = arg_str0("U", "user", "<string>", "Websocket server username");
    setArgs.pass = arg_str0("P", "pass", "<string>", "Websocket server password");
    setArgs.end = arg_end(6);

    return esp_console_cmd_register(&setCommand);
}