#include <esp_log.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "rc522.h"

#include "db/db.h"
#include "wifi/wifi.h"
#include "websocket/websocket.h"
#include "console.h"
#include "helper.h"

static const char *TAG = "lego-door-control";
static rc522_handle_t scanner;

static QueueHandle_t cardIdQueue = NULL;

static void rc522_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    rc522_event_data_t *data = (rc522_event_data_t *)event_data;

    switch (event_id) {
        case RC522_EVENT_TAG_SCANNED:
            rc522_tag_t *tag = (rc522_tag_t *)data->ptr;
            if (0xffffffffff < tag->serial_number) {
                ESP_LOGE(TAG, "Invalid serial number");
                return;
            }
            if (xQueueSendToBack(cardIdQueue, &(tag->serial_number), 0) != pdTRUE) {
                ESP_LOGE(TAG, "Failed to enqueue serial number.");
            }
            break;
        default:
            break;
    }
}

void app_main() {
    rc522_config_t config = {
        .spi.host = SPI2_HOST,
        .spi.miso_gpio = 2,
        .spi.mosi_gpio = 7,
        .spi.sck_gpio = 6,
        .spi.sda_gpio = 10,
    };

    cardIdQueue = xQueueCreate(16, sizeof(uint64_t));
    if (cardIdQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue. Rebooting.");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
        return;
    }

    consoleInit();

    dbInit();

    rc522_create(&config, &scanner);
    rc522_register_events(scanner, RC522_EVENT_ANY, rc522_handler, NULL);
    rc522_start(scanner);

    wifiInit();

    websocketInit();

    char *buf = malloc(256);

    for (;;) {
        uint64_t cardId;
        if (xQueueReceive(cardIdQueue, &cardId, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Scanned tag: "CARD_ID_FORMAT_STRING, CARD_ID_CONVERT(cardId));
            if (dbCheck(cardId) == ESP_OK) {
                ESP_LOGI(TAG, "Card OK");
                sprintf(buf, "{\"type\": \"swipe\", \"success\": true, \"card\": \"" CARD_ID_FORMAT_STRING "\"}", CARD_ID_CONVERT(cardId));
                websocketSendText(buf);
            } else {
                ESP_LOGW(TAG, "Card not allowed.");
                sprintf(buf, "{\"type\": \"swipe\", \"success\": false, \"card\": \"" CARD_ID_FORMAT_STRING "\"}", CARD_ID_CONVERT(cardId));
                websocketSendText(buf);
            }
        }
    }
}
