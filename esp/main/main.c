#include <esp_log.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#include "rc522.h"
#include "driver/rc522_spi.h"

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
static rc522_driver_handle_t driver;

static QueueHandle_t cardIdQueue = NULL;
static EventGroupHandle_t openDoor = NULL;

static uint64_t picc2uint64(rc522_picc_uid_t uid, uint8_t bytes) {
    uint64_t out = 0;
    for (uint8_t i = 0; i < MIN(uid.length, bytes); ++i)
        out ^= ((uint64_t)(uid.value[i])) << (i * 8);

    return out;
}

static void rc522_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    rc522_picc_state_changed_event_t *event = (rc522_picc_state_changed_event_t *)event_data;
    rc522_picc_t *picc = event->picc;

    if (picc->state == RC522_PICC_STATE_ACTIVE) {
        ESP_LOGI(TAG, "Serial length: %hhu", picc->uid.length);
        uint64_t id = picc2uint64(picc->uid, 6);
        if (0xffffffffffff < id) {
            ESP_LOGE(TAG, "Invalid serial number");
            return;
        }
        if (xQueueSendToBack(cardIdQueue, &id, 0) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to enqueue serial number.");
        }
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
    rc522_spi_config_t driver_config = {
        .host_id = SPI2_HOST,
        .bus_config = &(spi_bus_config_t) {
            .miso_io_num = 2,
            .mosi_io_num = 7,
            .sclk_io_num = 6,
        },
        .dev_config = {
            .spics_io_num = 10,
        },
        .rst_io_num = -1,
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

    // esp_log_level_set("rc522", ESP_LOG_DEBUG);

    rc522_spi_create(&driver_config, &driver);
    rc522_driver_install(driver);

    rc522_config_t scanner_config = {
        .driver = driver,
    };

    rc522_create(&scanner_config, &scanner);
    rc522_register_events(scanner, RC522_EVENT_PICC_STATE_CHANGED, rc522_handler, NULL);
    rc522_start(scanner);

    xTaskCreate(doorControlTask, "door-control", 2048, NULL, 7, NULL);

    esp_log_level_set("wifi", ESP_LOG_WARN);
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
