#include <stdlib.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/temperature_sensor.h"

#include "led/led.h"
#include "db/db.h"

#include "status/status.h"

static bool initialized = false;
static temperature_sensor_handle_t temp_handle = NULL;

esp_err_t statusInit() {
    if (initialized)
        return ESP_ERR_INVALID_STATE;

    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(0, 80);
    esp_err_t res = temperature_sensor_install(&temp_sensor_config, &temp_handle);
    if (res != ESP_OK)
        return res;

    initialized = true;
    return ESP_OK;
}

Status statusGet() {
    Status st;
    st.freeHeap = esp_get_free_heap_size();
    st.minFreeHeap = esp_get_minimum_free_heap_size();
    st.connection = ledGetWifiState() ? (ledGetWebsocketState() ? WEBSOCKET_CONNECTED : WEBSOCKET_DISCONNECTED) : WIFI_DISCONNECTED;
    st.dbSize = dbGetSize();
    st.uptime = esp_timer_get_time();

    temperature_sensor_enable(temp_handle);
    temperature_sensor_get_celsius(temp_handle, &st.temperature);
    temperature_sensor_disable(temp_handle);

    return st;
}

void statusSend(void (*wsSend)(const char *)) {
    Status st = statusGet();

    char *buf = malloc(512);
    sprintf(buf,
        "{\"freeHeap\": %lu, \"minFreeHeap\": %lu, \"connection\": \"%s\", \"dbSize\": %zu, \"uptime\": %lld, \"temperature\": %.2f}",
        st.freeHeap, st.minFreeHeap, statusConnection2str(st.connection), st.dbSize, st.uptime, st.temperature);
    wsSend(buf);
    free(buf);
}

const char *statusConnection2str(StatusConnection conn) {
    switch (conn) {
        case WIFI_DISCONNECTED:
            return "WIFI_DISCONNECTED";
        case WEBSOCKET_DISCONNECTED:
            return "WEBSOCKET_DISCONNECTED";
        case WEBSOCKET_CONNECTED:
            return "WEBSOCKET_CONNECTED";
        default:
            return "UNKNOWN";
    }
}
