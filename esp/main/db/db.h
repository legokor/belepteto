#pragma once

#include "esp_err.h"
#include <stdint.h>

esp_err_t dbInit();

esp_err_t dbAdd(uint64_t card);

esp_err_t dbRemove(uint64_t card);

esp_err_t dbRemoveAll();

esp_err_t dbCheck(uint64_t card);

esp_err_t dbBeginTransaction();

esp_err_t dbCommit();

esp_err_t dbRollback();

const uint64_t *dbGet();

size_t dbGetSize();
