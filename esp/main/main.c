#include <esp_log.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#include "rc522.h"

#include "db/db.h"
#include "wifi/wifi.h"
#include "websocket/websocket.h"
#include "console.h"
#include "helper.h"
#include "led/led.h"

#include "config.h"

#define CARD_ALLOWED BIT0
#define CARD_DENIED BIT1

static const char *TAG = "lego-door-control";
static rc522_handle_t scanner;

static QueueHandle_t cardIdQueue = NULL;
static EventGroupHandle_t openDoor = NULL;

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

    openDoor = xEventGroupCreate();
    if (openDoor == NULL) {
        ESP_LOGE(TAG, "Failed to create event group. Rebooting.");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
        return;
    }

    ledInit();

    consoleInit();

    dbInit();

    rc522_create(&config, &scanner);
    rc522_register_events(scanner, RC522_EVENT_ANY, rc522_handler, NULL);
    rc522_start(scanner);

    xTaskCreate(doorControlTask, "door-control", 2048, NULL, 7, NULL);

    wifiInit();

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
