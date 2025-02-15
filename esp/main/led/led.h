#pragma once

void ledInit();

void ledSetWiFiState(bool state);

bool ledGetWifiState();

void ledSetWebsocketState(bool state);

bool ledGetWebsocketState();

void ledSetDoorAllowedState(bool state);

void ledSetDoorDeniedState(bool state);

void ledSetDbTransaction(bool state);
