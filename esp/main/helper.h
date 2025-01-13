#pragma once

#define CARD_ID_FORMAT_STRING "%02X:%02X:%02X:%02X:%02X"

#define CARD_ID_CONVERT(cardId) (uint8_t)((cardId)>>0), (uint8_t)((cardId)>>8), (uint8_t)((cardId)>>16), (uint8_t)((cardId)>>24), (uint8_t)((cardId)>>32)
