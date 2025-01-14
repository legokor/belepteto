#include <stdio.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"

#include "websocket/websocket.h"
#include "storage_wrapper.h"
#include "db/db.h"
#include "config.h"
#include "helper.h"
#include "led/led.h"

static const char *TAG = "WEBSOCKET";

static esp_websocket_client_handle_t client;
static QueueHandle_t websocketTextQueue = NULL;

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
            ledSetWebsocketState(true);
            esp_websocket_client_send_text(client, "{\"type\": \"connected\"}", 21, portMAX_DELAY);
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
            ledSetWebsocketState(false);
            break;
        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == 10)
                break;
            // ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
            // ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
            if (data->op_code == 1) {
                char *text = malloc(data->data_len + 1);
                memcpy(text, data->data_ptr, data->data_len);
                text[data->data_len] = '\0';
                if (xQueueSend(websocketTextQueue, &text, 0) != pdTRUE) {
                    free(text);
                    ESP_LOGE(TAG, "Error sending to queue");
                }
            }
            break;
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
            break;
    }
}

static void websocket_app_start(void) {
    esp_websocket_client_config_t websocket_cfg = {};
    websocket_cfg.task_prio = 2;
    websocket_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    websocket_cfg.reconnect_timeout_ms = 10000;
    websocket_cfg.network_timeout_ms = 10000;

    char *buf = malloc(64);
    storageGetString("wsURI", buf, "ws://127.0.0.1/", 64, NULL);
    websocket_cfg.uri = buf;
    buf = malloc(64);
    storageGetString("wsUser", buf, "", 64, NULL);
    websocket_cfg.username = buf;
    buf = malloc(64);
    storageGetString("wsPass", buf, "", 64, NULL);
    websocket_cfg.password = buf;

    ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);
    ESP_LOGI(TAG, "Username: %s", websocket_cfg.username);
    ESP_LOGI(TAG, "Password: %s", websocket_cfg.password);
    ESP_LOGI(TAG, "Task Priority: %d", websocket_cfg.task_prio);

    client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);

    esp_websocket_client_start(client);
}

static void websockerProcessTask(void *arg) {
    char *data;
    for (;;) {
        if (xQueueReceive(websocketTextQueue, &data, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Received: %s", data);
            char *cmd = data;
            char *arg = "";
            for (char *i = data; *i != '\0'; i++) {
                if (*i == ' ') {
                    *i = '\0';
                    arg = i + 1;
                    break;
                }
            }

            if (strcmp(cmd, "CLEAR") == 0) {
                if (arg[0] != 0) {
                    websocketSendText("{\"type\": \"clear\", \"success\": false, \"message\": \"requires no args\"}");
                    ESP_LOGE(TAG, "CLEAR: called with arg %s", arg);
                } else {
                    ESP_LOGI(TAG, "CLEAR: clearing database");

                    esp_err_t err = dbRemoveAll();

                    switch (err) {
                        case ESP_OK:
                            websocketSendText("{\"type\": \"clear\", \"success\": true}");
                            ESP_LOGI(TAG, "CLEAR: OK");
                            break;
                        case ESP_ERR_INVALID_STATE:
                            websocketSendText("{\"type\": \"clear\", \"success\": false, \"message\": \"invalid state\"}");
                            ESP_LOGE(TAG, "CLEAR: invalid state");
                            break;
                        default:
                            websocketSendText("{\"type\": \"clear\", \"success\": false, \"message\": \"general error\"}");
                            ESP_LOGE(TAG, "CLEAR: general error");
                            break;
                    }
                }
            } else if (strcmp(cmd, "ADD") == 0) {
                uint64_t id;
                uint8_t *uId = (uint8_t *)&id;
                if (sscanf(arg, "%hhX:%hhX:%hhX:%hhX:%hhX", uId, uId + 1, uId + 2, uId + 3, uId + 4) == 5) {
                    ESP_LOGI(TAG, "ADD: adding card " CARD_ID_FORMAT_STRING, CARD_ID_CONVERT(id));

                    esp_err_t err = dbAdd(id);

                    switch (err) {
                        case ESP_OK:
                            websocketSendText("{\"type\": \"add\", \"success\": true}");
                            ESP_LOGI(TAG, "ADD: OK");
                            break;
                        case ESP_ERR_NO_MEM:
                            websocketSendText("{\"type\": \"add\", \"success\": false, \"message\": \"no memory\"}");
                            ESP_LOGE(TAG, "ADD: no memory");
                            break;
                        case ESP_ERR_INVALID_STATE:
                            websocketSendText("{\"type\": \"add\", \"success\": false, \"message\": \"invalid state\"}");
                            ESP_LOGE(TAG, "ADD: invalid state");
                            break;
                        default:
                            websocketSendText("{\"type\": \"add\", \"success\": false, \"message\": \"general error\"}");
                            ESP_LOGE(TAG, "ADD: general error");
                            break;
                    }
                } else {
                    websocketSendText("{\"type\": \"add\", \"success\": false, \"message\": \"invalid card id\"}");
                    ESP_LOGE(TAG, "ADD: invalid id: %s", arg);
                }
            } else if (strcmp(cmd, "REMOVE") == 0) {
                uint64_t id;
                uint8_t *uId = (uint8_t *)&id;
                if (sscanf(arg, "%hhX:%hhX:%hhX:%hhX:%hhX", uId, uId + 1, uId + 2, uId + 3, uId + 4) == 5) {
                    ESP_LOGI(TAG, "REMOVE: removing card " CARD_ID_FORMAT_STRING, CARD_ID_CONVERT(id));

                    esp_err_t err = dbRemove(id);

                    switch (err) {
                        case ESP_OK:
                            websocketSendText("{\"type\": \"remove\", \"success\": true}");
                            ESP_LOGI(TAG, "REMOVE: OK");
                            break;
                        case ESP_ERR_INVALID_STATE:
                            websocketSendText("{\"type\": \"remove\", \"success\": false, \"message\": \"invalid state\"}");
                            ESP_LOGE(TAG, "REMOVE: invalid state");
                            break;
                        case ESP_ERR_NOT_FOUND:
                            websocketSendText("{\"type\": \"remove\", \"success\": false, \"message\": \"card not found\"}");
                            ESP_LOGE(TAG, "REMOVE: card not found");
                            break;
                        default:
                            websocketSendText("{\"type\": \"remove\", \"success\": false, \"message\": \"general error\"}");
                            ESP_LOGE(TAG, "REMOVE: general error");
                            break;
                    }
                } else {
                    websocketSendText("{\"type\": \"remove\", \"success\": false, \"message\": \"invalid card id\"}");
                    ESP_LOGE(TAG, "REMOVE: invalid id: %s", arg);
                }
            } else if (strcmp(cmd, "BEGIN_TRANSACTION") == 0) {
                if (arg[0] != 0) {
                    websocketSendText("{\"type\": \"begin_transaction\", \"success\": false, \"message\": \"requires no args\"}");
                    ESP_LOGE(TAG, "BEGIN_TRANSACTION: called with arg %s", arg);
                } else {
                    esp_err_t err = dbBeginTransaction();

                    switch (err) {
                        case ESP_OK:
                            websocketSendText("{\"type\": \"begin_transaction\", \"success\": true}");
                            ESP_LOGI(TAG, "BEGIN_TRANSACTION: OK");
                            break;
                        case ESP_ERR_INVALID_STATE:
                            websocketSendText("{\"type\": \"begin_transaction\", \"success\": false, \"message\": \"invalid state\"}");
                            ESP_LOGE(TAG, "BEGIN_TRANSACTION: invalid state");
                            break;
                        default:
                            websocketSendText("{\"type\": \"begin_transaction\", \"success\": false, \"message\": \"general error\"}");
                            ESP_LOGE(TAG, "BEGIN_TRANSACTION: general error");
                            break;
                    }
                }
            } else if (strcmp(cmd, "ROLLBACK") == 0) {
                if (arg[0] != 0) {
                    websocketSendText("{\"type\": \"rollback\", \"success\": false, \"message\": \"requires no args\"}");
                    ESP_LOGE(TAG, "ROLLBACK: called with arg %s", arg);
                } else {
                    esp_err_t err = dbRollback();

                    switch (err) {
                        case ESP_OK:
                            websocketSendText("{\"type\": \"rollback\", \"success\": true}");
                            ESP_LOGI(TAG, "ROLLBACK: OK");
                            break;
                        case ESP_ERR_INVALID_STATE:
                            websocketSendText("{\"type\": \"rollback\", \"success\": false, \"message\": \"invalid state\"}");
                            ESP_LOGE(TAG, "ROLLBACK: invalid state");
                            break;
                        default:
                            websocketSendText("{\"type\": \"rollback\", \"success\": false, \"message\": \"general error\"}");
                            ESP_LOGE(TAG, "ROLLBACK: general error");
                            break;
                    }
                }
            } else if (strcmp(cmd, "COMMIT") == 0) {
                if (arg[0] != 0) {
                    websocketSendText("{\"type\": \"commit\", \"success\": false, \"message\": \"requires no args\"}");
                    ESP_LOGE(TAG, "COMMIT: called with arg %s", arg);
                } else {
                    esp_err_t err = dbCommit();

                    switch (err) {
                        case ESP_OK:
                            websocketSendText("{\"type\": \"commit\", \"success\": true}");
                            ESP_LOGI(TAG, "COMMIT: OK");
                            break;
                        case ESP_ERR_INVALID_STATE:
                            websocketSendText("{\"type\": \"commit\", \"success\": false, \"message\": \"invalid state\"}");
                            ESP_LOGE(TAG, "COMMIT: invalid state");
                            break;
                        default:
                            websocketSendText("{\"type\": \"commit\", \"success\": false, \"message\": \"general error\"}");
                            ESP_LOGE(TAG, "COMMIT: general error");
                            break;
                    }
                }
            } else if (strcmp(cmd, "GET") == 0) {
                char *buf = malloc(20 * sizeof(char) * CONFIG_MAX_CARDS + 100);
                char *smolbuf = malloc(25 * sizeof(char));
                const uint64_t *cards = dbGet();
                bool first = true;
                strcpy(buf, "{\"type\": \"get\", \"cards\": [");
                for (size_t idx = 0; idx < CONFIG_MAX_CARDS; ++idx) {
                    if (!(cards[idx] & 0xffffffffff))
                        continue;

                    if (first) {
                        first = false;
                        sprintf(smolbuf, "\"" CARD_ID_FORMAT_STRING "\"", CARD_ID_CONVERT(cards[idx]));
                    } else {
                        sprintf(smolbuf, ", \"" CARD_ID_FORMAT_STRING "\"", CARD_ID_CONVERT(cards[idx]));
                    }

                    strcat(buf, smolbuf);
                }
                strcat(buf, "]}");
                websocketSendText(buf);
                free(smolbuf);
                free(buf);
            } else {
                websocketSendText("{\"type\": \"error\", \"message\": \"unknown command\"}");
            }

            free(data);
        }
    }
}

void websocketInit() {
    websocketTextQueue = xQueueCreate(10, sizeof(char *));

    websocket_app_start();

    xTaskCreate(websockerProcessTask, "websockerProcessTask", 4096, NULL, 2, NULL);
}

void websocketDestroy() {
    esp_websocket_client_stop(client);
    ESP_LOGI(TAG, "Websocket Stopped");
    esp_websocket_client_destroy(client);
}

void websocketSendText(const char *text) {
    esp_websocket_client_send_text(client, text, strlen(text), portMAX_DELAY);
}
