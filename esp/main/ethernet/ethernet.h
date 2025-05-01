#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

esp_err_t ethernetInit(EventGroupHandle_t gotIp);
