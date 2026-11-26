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

// One-pole low-pass on the published current. 0 turns the filter off.
// 0.8 s removes the ADC fuzz and still follows a real change within a couple of seconds.
constexpr float FILTER_TAU_S = 0.8f;
constexpr uint32_t CHART_WINDOW_MS = 60000;

constexpr uint16_t HTTP_PORT = 80;
constexpr char MDNS_HOST[] = "persianas";

// Power and direction relay inputs. GPIO2 and GPIO3 are not strapping pins.
constexpr int PIN_MOTOR_POWER = 2;
constexpr int PIN_MOTOR_DIR = 3;

// Most relay modules energize when the input is pulled low.
constexpr bool RELAY_ACTIVE_LOW = true;

// GPIO level that selects the open direction. Close is the opposite level.
constexpr bool DIRECTION_OPEN_HIGH = true;

// Ignore current after the motor is powered. Startup inrush looks like a bumper.
constexpr uint32_t MOTOR_BLANKING_MS = 1000;

// Direction relay changes only while power is off, then power follows.
constexpr uint32_t MOTOR_DIR_SETTLE_MS = 30;

// Faster than the chart filter so a bumper stops the motor promptly.
constexpr float ENDSTOP_TAU_S = 0.2f;

constexpr float DEFAULT_THRESHOLD_A = 1.0f;
constexpr uint16_t DEFAULT_OPEN_S = 25;
constexpr uint16_t DEFAULT_CLOSE_S = 25;

constexpr float THRESHOLD_MIN_A = 0.05f;
constexpr float THRESHOLD_MAX_A = 3.0f;
constexpr uint16_t TRAVEL_MIN_S = 1;
constexpr uint16_t TRAVEL_MAX_S = 180;

constexpr char MQTT_CLIENT_ID[] = "persianas";
constexpr char MQTT_COMMAND_TOPIC[] = "persianas/cover/set";
constexpr char MQTT_STATE_TOPIC[] = "persianas/cover/state";
constexpr char MQTT_AVAIL_TOPIC[] = "persianas/status";
constexpr char MQTT_DISCOVERY_TOPIC[] = "homeassistant/cover/persianas/config";

// Onboard WS2812 is very bright at full scale.
constexpr uint8_t LED_LEVEL = 24;
