#pragma once

void ledInit();

void ledSetWiFiState(bool state);

void ledSetWebsocketState(bool state);

void ledSetDoorAllowedState(bool state);

void ledSetDoorDeniedState(bool state);

void ledSetDbTransaction(bool state);
