#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "led_strip.h"

#include "db/db.h"

#include "config.h"

#include "led.h"

#if (CONFIG_LED_STRIP_BRIGHTNESS > 85)
#error "Brightness must be <85."
#endif

typedef enum {
    OFF = 0b000,
    RED = 0b100,
    GREEN = 0b010,
    BLUE = 0b001,
    YELLOW = 0b110,
    MAGENTA = 0b101,
    CYAN = 0b011,
    WHITE = 0b111,
} LedColor;

static LedColor color = OFF;

static led_strip_handle_t led_strip;

static bool dbTransaction = false;
static bool wifiConnected = false;
static bool websocketConnected = false;
static bool doorAllowed = false;
static bool doorDenied = false;

static void updateColor() {
    if (dbTransaction) {
        if (doorAllowed || doorDenied) {
            color = WHITE;
        } else {
            color = BLUE;
        }
    } else {
        if (doorAllowed) {
            color = GREEN;
        } else if (doorDenied) {
            color = RED;
        } else {
            if (wifiConnected) {
                if (websocketConnected) {
                    color = CYAN;
                } else {
                    color = YELLOW;
                }
            } else {
                color = MAGENTA;
            }
        }
    }

    bool r = ((color & 0b100) >> 2);
    bool g = ((color & 0b010) >> 1);
    bool b = (color & 0b001);
    uint8_t brightness = CONFIG_LED_STRIP_BRIGHTNESS * 3 / (r + g + b);

    for (uint8_t i = 0; i < CONFIG_LED_STRIP_LENGTH; i++) {
        led_strip_set_pixel(led_strip, i, r * brightness, g * brightness, b * brightness);
    }
    led_strip_refresh(led_strip);
}

void ledInit() {
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_LED_STRIP_PIN,
        .max_leds = CONFIG_LED_STRIP_LENGTH,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    updateColor();
}

void ledSetWiFiState(bool state) {
    wifiConnected = state;
    updateColor();
}

void ledSetWebsocketState(bool state) {
    websocketConnected = state;
    updateColor();
}

void ledSetDoorAllowedState(bool state) {
    doorAllowed = state;
    updateColor();
}

void ledSetDoorDeniedState(bool state) {
    doorDenied = state;
    updateColor();
}

void ledSetDbTransaction(bool state) {
    dbTransaction = state;
    updateColor();
}