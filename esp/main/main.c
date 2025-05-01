#include <esp_log.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#include "pn532.h"

#include "db/db.h"
#include "wifi/wifi.h"
#include "ethernet/ethernet.h"
#include "websocket/websocket.h"
#include "console.h"
#include "helper.h"
#include "led/led.h"
#include "status/status.h"

#include "config.h"

#define CARD_ALLOWED BIT0
#define CARD_DENIED BIT1

static const char *TAG = "lego-door-control";

static pn532_t *pn532;

static QueueHandle_t cardIdQueue = NULL;
static EventGroupHandle_t openDoor = NULL;
static EventGroupHandle_t gotIp = NULL;

// static uint64_t picc2uint64(rc522_picc_uid_t uid, uint8_t bytes) {
//     uint64_t out = 0;
//     for (uint8_t i = 0; i < MIN(uid.length, bytes); ++i)
//         out ^= ((uint64_t)(uid.value[i])) << (i * 8);

//     return out;
// }

static void pn532ToQueue(uint8_t *buf, size_t len) {
    if (len < 1) {
        ESP_LOGW(TAG, "Data too short");
        return;
    }

    uint8_t cardCount = buf[0];
    if (cardCount == 0)
        return;
    if (cardCount > 2) {
        ESP_LOGW(TAG, "Invalid card count: %hhu", cardCount);
        return;
    }

    uint8_t *bufEnd = buf + len;
    ++buf;

    for (size_t card = 0; card < cardCount; ++card) {
        buf += 4;
        if (buf >= bufEnd) {
            ESP_LOGW(TAG, "Data ended before expected");
            return;
        }

        uint8_t uidLength = *buf;
        ++buf;
        if (buf + uidLength - 1 >= bufEnd) {
            ESP_LOGW(TAG, "Data ended before expected");
            return;
        }

        uint64_t uid = 0;
        for (uint8_t i = 0; i < MIN(uidLength, 6); ++i)
            uid ^= ((uint64_t)(buf[i])) << (i * 8);

        xQueueSend(cardIdQueue, &uid, 0);

        buf += uidLength;
    }
}

static void pn532Task(void *arg) {
    uint8_t buf[4] = { 0x05, 0xff, 0x01, 0xff };
    if (pn532_tx(pn532, 0x32, 0, NULL, 4, buf) < 0 || pn532_rx(pn532, 0, NULL, sizeof(buf), buf, 50) < 0) {
        ESP_LOGE(TAG, "RFConfiguration fail %s", pn532_err_to_name(pn532_lasterr(pn532)));
        pn532_end(pn532);
        vTaskDelete(NULL);
    }

    uint8_t *cardBuf = malloc(128);

    for (;;) {
        pn532_ILPT_Send(pn532);
        int len = pn532_rx(pn532, 0, NULL, 128, cardBuf, 60000);

        if (len <= 0) {
            continue;
        }

        if (len >= 128) {
            ESP_LOGW(TAG, "len >= 128: %d", len);
            len = 127;
        }
        ESP_LOG_BUFFER_HEX(TAG, cardBuf, len);
        pn532ToQueue(cardBuf, len);

    }
}

static void doorControlTask(void *arg) {
    gpio_set_direction(CONFIG_DOOR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(CONFIG_DOOR_PIN, CONFIG_DOOR_ACTIVE_LOW);

    EventBits_t bits;

    for (;;) {
        if ((bits = xEventGroupWaitBits(openDoor, CARD_ALLOWED | CARD_DENIED, pdTRUE, pdFALSE, portMAX_DELAY)) != 0) {
            if (bits & CARD_ALLOWED) {
                gpio_set_level(CONFIG_DOOR_PIN, !CONFIG_DOOR_ACTIVE_LOW);
                ledSetDoorAllowedState(true);
                ESP_LOGI(TAG, "Door open");
            } else {
                ledSetDoorDeniedState(true);
                ESP_LOGI(TAG, "Card denied");
            }
            if (!(xEventGroupWaitBits(openDoor, CARD_ALLOWED, pdFALSE, pdTRUE, pdMS_TO_TICKS(CONFIG_DOOR_OPEN_TIME_MS)) & CARD_ALLOWED)) {
                gpio_set_level(CONFIG_DOOR_PIN, CONFIG_DOOR_ACTIVE_LOW);
                ledSetDoorAllowedState(false);
                ESP_LOGI(TAG, "Door closed");
            }
            xEventGroupClearBits(openDoor, CARD_DENIED);
            ledSetDoorDeniedState(false);
        }
    }

    vTaskDelete(NULL);
}

void app_main() {
    pn532 = pn532_init(UART_NUM_1, 4, CONFIG_PN532_TX, CONFIG_PN532_RX, 0);

    cardIdQueue = xQueueCreate(16, sizeof(uint64_t));
    if (cardIdQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue. Rebooting.");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
        return;
    }

    openDoor = xEventGroupCreate();
    if (openDoor == NULL) {
        ESP_LOGE(TAG, "Failed to create event group. Rebooting.");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
        return;
    }

    gotIp = xEventGroupCreate();
    if (gotIp == NULL) {
        ESP_LOGE(TAG, "Failed to create event group. Rebooting.");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
        return;
    }

    ledInit();

    statusInit();

    consoleInit();

    dbInit();

    // esp_log_level_set("rc522", ESP_LOG_DEBUG);

    xTaskCreate(doorControlTask, "door-control", 2048, NULL, 7, NULL);

    xTaskCreate(pn532Task, "pn532", 2048, NULL, 8, NULL);

    // esp_log_level_set("wifi", ESP_LOG_WARN);
    // wifiInit();
    ethernetInit(gotIp);

    xEventGroupWaitBits(gotIp, BIT0, true, false, pdMS_TO_TICKS(10000));
    websocketInit();

    char *buf = malloc(256);

    for (;;) {
        uint64_t cardId;
        if (xQueueReceive(cardIdQueue, &cardId, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Scanned tag: "CARD_ID_FORMAT_STRING, CARD_ID_CONVERT(cardId));
            if (dbCheck(cardId) == ESP_OK) {
                xEventGroupSetBits(openDoor, CARD_ALLOWED);
                ESP_LOGI(TAG, "Card OK");
                sprintf(buf, "{\"type\": \"swipe\", \"success\": true, \"card\": \"" CARD_ID_FORMAT_STRING "\"}", CARD_ID_CONVERT(cardId));
                websocketSendText(buf);
            } else {
                xEventGroupSetBits(openDoor, CARD_DENIED);
                ESP_LOGW(TAG, "Card not allowed.");
                sprintf(buf, "{\"type\": \"swipe\", \"success\": false, \"card\": \"" CARD_ID_FORMAT_STRING "\"}", CARD_ID_CONVERT(cardId));
                websocketSendText(buf);
            }
        }
    }
}
