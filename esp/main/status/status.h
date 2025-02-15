#pragma once

#include "esp_types.h"

typedef enum {
    WIFI_DISCONNECTED,
    WEBSOCKET_DISCONNECTED,
    WEBSOCKET_CONNECTED
} StatusConnection;

typedef struct {
    float temperature;
    uint32_t freeHeap;
    uint32_t minFreeHeap;
    size_t dbSize;
    StatusConnection connection;
    int64_t uptime;
} Status;

esp_err_t statusInit();

void statusSend(void (*wsSend)(const char *));

Status statusGet();

const char *statusConnection2str(StatusConnection conn);
