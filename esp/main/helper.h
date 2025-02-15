#pragma once

#define CARD_ID_FORMAT_STRING "%02X:%02X:%02X:%02X:%02X:%02X"

#define CARD_ID_CONVERT(cardId) (uint8_t)((cardId)>>0), (uint8_t)((cardId)>>8), (uint8_t)((cardId)>>16), (uint8_t)((cardId)>>24), (uint8_t)((cardId)>>32), (uint8_t)((cardId)>>40)

#define SCANF_CARD_FORMAT_STRING "%hhX:%hhX:%hhX:%hhX:%hhX:%hhX"

#define SCANF_CARD_ID_UINT_CONVERT(cardId) (cardId), (cardId + 1), (cardId + 2), (cardId + 3), (cardId + 4), (cardId + 5)

#define SCANF_CARD_ID_LENGTH 6
