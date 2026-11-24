#pragma once

#include <Arduino.h>

// MAX471 OUT, or a breakout's AT pin (1 V/A), to GPIO0 / A0.
constexpr int PIN_OUT = 0;

// Open-collector SIGN output. The pin's internal pull-up is enabled.
// Datasheet: low means current is flowing from RS- toward RS+.
constexpr int PIN_SIGN = 1;

// 1.0 for OUT/AT when ROUT is 2 kOhm. Use 0.2 if the module's VT pin is wired.
constexpr float VOLTS_PER_AMP = 1.0f;

// Subtracted from the averaged OUT voltage before the V/A scale is applied.
constexpr float ZERO_MV = 0.0f;

// When true, a low SIGN pin is plotted as a negative current.
constexpr bool SIGN_LOW_IS_REVERSE = true;

constexpr int SAMPLE_HZ = 10;
constexpr int ADC_SAMPLES = 16;
constexpr uint32_t CHART_WINDOW_MS = 60000;

constexpr uint16_t HTTP_PORT = 80;
constexpr char MDNS_HOST[] = "max471";

// Onboard WS2812 is very bright at full scale.
constexpr uint8_t LED_LEVEL = 24;
