#pragma once

#include <Arduino.h>

// Relay outputs off, then load the saved threshold and travel times.
void shutterBegin();
void shutterLogConfig();

// Motor timers, deferred MQTT commands, and the broker connection.
// Reconnect is blocking, so only the main loop may allow it.
void shutterService(bool allowReconnect);

// Absolute amps, before the slower chart filter.
void shutterOnCurrent(float absAmps, uint32_t now);

const char* shutterState();
bool shutterCommand(const char* cmd);
bool shutterSaveSettings(float thresholdA, long openSeconds, long closeSeconds);
int shutterWriteSettingsJson(char* buf, size_t length);
