#include "db/db.h"

#include "stdbool.h"
#include "esp_crc.h"
#include "esp_log.h"

#include "storage_wrapper.h"
#include "led/led.h"

#include "config.h"

static const char *TAG = "card-db";

static uint64_t *db = NULL;
static size_t count = 0;
static bool initialized = false;
static bool transaction = false;

/*
 * Card id layout
 * | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
 * | CRC16 | <----- CARD ID -----> |
 *
 * CRC16 of card id
 */

static void genCRC(uint64_t *card) {
    uint16_t crc = esp_crc16_be(CONFIG_CRC_START_VALUE, (uint8_t *)card, 6);
    ((uint16_t *)card)[3] = crc;
}

static bool checkCRC(uint64_t *card) {
    uint16_t crc = esp_crc16_be(CONFIG_CRC_START_VALUE, (uint8_t *)card, 6);
    return ((uint16_t *)card)[3] == crc;
}

static void truncateId(uint64_t *card) {
    if (0xffffffffffff < *card) {
        ESP_LOGW(TAG, "Card ID too large for 6 byte ID, truncating.");
        *card &= 0xffffffffffff;
    }
}

static uint64_t *find(uint64_t card) {
    truncateId(&card);
    genCRC(&card);

    for (size_t idx = 0; idx < CONFIG_MAX_CARDS; ++idx) {
        if (db[idx] == card)
            return db + idx;
    }

    return NULL;
}

static void compactDb() {
    size_t writeIdx = 0;
    for (size_t idx = 0; idx < CONFIG_MAX_CARDS; ++idx) {
        if (db[idx] != 0 && checkCRC(&db[idx])) {
            if (writeIdx != idx)
                db[writeIdx] = db[idx];

            ++writeIdx;
        } else if (db[idx] != 0) {
            ESP_LOGE(TAG, "Invalid CRC at idx %u", idx);
        }
    }

    count = writeIdx;

    for (size_t idx = count; idx < CONFIG_MAX_CARDS; ++idx) {
        db[idx] = 0;
    }
}

esp_err_t dbInit() {
    if (initialized)
        return ESP_ERR_INVALID_STATE;

    db = calloc(CONFIG_MAX_CARDS, sizeof(uint64_t));
    if (db == NULL)
        return ESP_ERR_NO_MEM;

    esp_err_t err = storageInit();
    if (err != ESP_OK) {
        free(db);
        return err;
    }

    count = 0;
    err = storageGetBlob("card-db", db, &count, CONFIG_MAX_CARDS * sizeof(uint64_t), NULL);
    count /= sizeof(uint64_t);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        free(db);
        return err;
    }

    compactDb();

    initialized = true;

    return ESP_OK;
}

esp_err_t dbAdd(uint64_t card) {
    if (!initialized)
        return ESP_ERR_INVALID_STATE;

    if (!transaction)
        return ESP_ERR_INVALID_STATE;

    if (count >= CONFIG_MAX_CARDS)
        return ESP_ERR_NO_MEM;

    uint64_t *location = find(card);
    if (location != NULL) {
        ESP_LOGW(TAG, "Card already in DB, not adding.");
        return ESP_OK;
    }

    truncateId(&card);
    genCRC(&card);

    db[count] = card;
    ++count;

    return ESP_OK;
}

esp_err_t dbRemove(uint64_t card) {
    if (!initialized)
        return ESP_ERR_INVALID_STATE;

    if (!transaction)
        return ESP_ERR_INVALID_STATE;

    uint64_t *location = find(card);
    if (location == NULL)
        return ESP_ERR_NOT_FOUND;

    *location = 0;
    compactDb();

    return ESP_OK;
}

esp_err_t dbRemoveAll() {
    if (!initialized)
        return ESP_ERR_INVALID_STATE;

    if (!transaction)
        return ESP_ERR_INVALID_STATE;

    memset(db, 0, CONFIG_MAX_CARDS * sizeof(uint64_t));
    count = 0;

    return ESP_OK;
}

esp_err_t dbCheck(uint64_t card) {
    if (!initialized)
        return ESP_ERR_INVALID_STATE;

    if (transaction)
        return ESP_ERR_INVALID_STATE;

    uint64_t *location = find(card);
    if (location == NULL)
        return ESP_ERR_NOT_FOUND;

    return ESP_OK;
}

esp_err_t dbBeginTransaction() {
    if (!initialized)
        return ESP_ERR_INVALID_STATE;

    transaction = true;
    ledSetDbTransaction(true);

    return ESP_OK;
}

esp_err_t dbRollback() {
    if (!initialized)
        return ESP_ERR_INVALID_STATE;

    if (!transaction)
        return ESP_ERR_INVALID_STATE;

    count = 0;
    esp_err_t err = storageGetBlob("card-db", db, &count, CONFIG_MAX_CARDS * sizeof(uint64_t), NULL);
    count /= sizeof(uint64_t);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        free(db);
        return err;
    }

    transaction = false;
    ledSetDbTransaction(false);

    return ESP_OK;
}

esp_err_t dbCommit() {
    if (!initialized)
        return ESP_ERR_INVALID_STATE;

    if (!transaction)
        return ESP_ERR_INVALID_STATE;

    esp_err_t err = storageSetBlob("card-db", db, count * sizeof(uint64_t));

    if (err == ESP_OK) {
        transaction = false;
        ledSetDbTransaction(false);
    }

    return err;
}

const uint64_t *dbGet() {
    return db;
}
