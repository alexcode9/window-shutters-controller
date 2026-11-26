#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include <cmath>
#include <cstring>
#include <driver/gpio.h>

#include "config.h"
#include "shutter.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing include/secrets.h. Copy include/secrets.h.example to include/secrets.h."
#endif

#ifndef MQTT_HOST
#error "include/secrets.h needs MQTT_HOST, MQTT_PORT, MQTT_USER, and MQTT_PASSWORD."
#endif
#ifndef MQTT_PORT
#error "include/secrets.h needs MQTT_PORT."
#endif
#ifndef MQTT_USER
#error "include/secrets.h needs MQTT_USER."
#endif
#ifndef MQTT_PASSWORD
#error "include/secrets.h needs MQTT_PASSWORD."
#endif

namespace {

enum class Motion : uint8_t { Stopped, Opening, Closing, Open, Closed };
enum class Phase : uint8_t { Idle, Settling, Running };

constexpr char DISCOVERY_JSON[] =
    "{\"name\":\"Persiana\",\"unique_id\":\"persianas_cover\","
    "\"command_topic\":\"persianas/cover/set\","
    "\"state_topic\":\"persianas/cover/state\","
    "\"availability_topic\":\"persianas/status\","
    "\"payload_open\":\"OPEN\",\"payload_close\":\"CLOSE\",\"payload_stop\":\"STOP\","
    "\"state_open\":\"open\",\"state_closed\":\"closed\","
    "\"state_opening\":\"opening\",\"state_closing\":\"closing\",\"state_stopped\":\"stopped\","
    "\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
    "\"device_class\":\"shutter\",\"qos\":1,\"retain\":true,"
    "\"device\":{\"identifiers\":[\"persianas\"],\"name\":\"Persiana\","
    "\"model\":\"ESP32-C6-Zero\",\"manufacturer\":\"Waveshare\"}}";

Preferences prefs;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

float thresholdA = DEFAULT_THRESHOLD_A;
uint16_t openSeconds = DEFAULT_OPEN_S;
uint16_t closeSeconds = DEFAULT_CLOSE_S;
bool prefsOpen = false;

Motion motion = Motion::Stopped;
Phase phase = Phase::Idle;
uint32_t settleUntil = 0;
uint32_t poweredAt = 0;

float endstopAmps = 0.0f;
bool endstopPrimed = false;
uint32_t lastEndstopMs = 0;

char pendingCmd[8];
bool pending = false;
bool stateDirty = true;
uint32_t nextMqttAt = 0;
bool mqttConfigured = false;

const char* motionName(Motion value) {
  switch (value) {
    case Motion::Opening:
      return "opening";
    case Motion::Closing:
      return "closing";
    case Motion::Open:
      return "open";
    case Motion::Closed:
      return "closed";
    case Motion::Stopped:
    default:
      return "stopped";
  }
}

bool moving() {
  return motion == Motion::Opening || motion == Motion::Closing;
}

void driveLevel(int pin, int level) {
  gpio_set_level(static_cast<gpio_num_t>(pin), level);
}

void driveOutput(int pin, int level) {
  driveLevel(pin, level);
  gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT);
  driveLevel(pin, level);
}

int powerLevel(bool on) {
  if (RELAY_ACTIVE_LOW) {
    return on ? 0 : 1;
  }
  return on ? 1 : 0;
}

int directionLevel(bool openDir) {
  const bool high = openDir ? DIRECTION_OPEN_HIGH : !DIRECTION_OPEN_HIGH;
  return high ? 1 : 0;
}

void writePower(bool on) {
  driveLevel(PIN_MOTOR_POWER, powerLevel(on));
}

void writeDirection(bool openDir) {
  driveLevel(PIN_MOTOR_DIR, directionLevel(openDir));
}

bool thresholdOk(float value) {
  return value >= THRESHOLD_MIN_A && value <= THRESHOLD_MAX_A;
}

bool secondsOk(long value) {
  return value >= TRAVEL_MIN_S && value <= TRAVEL_MAX_S;
}

void loadSettings() {
  prefsOpen = prefs.begin("shutter", false);
  if (!prefsOpen) {
    return;
  }

  float loadedThreshold = prefs.getFloat("thr", DEFAULT_THRESHOLD_A);
  uint16_t loadedOpen = prefs.getUShort("open", DEFAULT_OPEN_S);
  uint16_t loadedClose = prefs.getUShort("close", DEFAULT_CLOSE_S);

  if (!thresholdOk(loadedThreshold)) {
    loadedThreshold = DEFAULT_THRESHOLD_A;
    prefs.putFloat("thr", loadedThreshold);
  }
  if (!secondsOk(loadedOpen)) {
    loadedOpen = DEFAULT_OPEN_S;
    prefs.putUShort("open", loadedOpen);
  }
  if (!secondsOk(loadedClose)) {
    loadedClose = DEFAULT_CLOSE_S;
    prefs.putUShort("close", loadedClose);
  }

  thresholdA = loadedThreshold;
  openSeconds = loadedOpen;
  closeSeconds = loadedClose;
}

void finishMove(Motion next, const char* reason) {
  writePower(false);
  phase = Phase::Idle;
  motion = next;
  stateDirty = true;
  Serial.print("motor ");
  Serial.println(reason);
}

void beginMove(bool openDir) {
  writePower(false);
  writeDirection(openDir);
  motion = openDir ? Motion::Opening : Motion::Closing;
  phase = Phase::Settling;
  settleUntil = millis() + MOTOR_DIR_SETTLE_MS;
  endstopPrimed = false;
  stateDirty = true;
  Serial.println(openDir ? "motor opening" : "motor closing");
}

void requestMove(bool openDir) {
  if (openDir && (motion == Motion::Opening || motion == Motion::Open)) {
    return;
  }
  if (!openDir && (motion == Motion::Closing || motion == Motion::Closed)) {
    return;
  }
  beginMove(openDir);
}

void requestStop() {
  if (!moving()) {
    return;
  }
  finishMove(Motion::Stopped, "stop");
}

bool normalize(const char* raw, size_t length, char* out, size_t outLen) {
  size_t i = 0;
  size_t o = 0;
  while (i < length && (raw[i] == ' ' || raw[i] == '\r' || raw[i] == '\n')) {
    i++;
  }
  while (i < length && raw[i] != ' ' && raw[i] != '\r' && raw[i] != '\n' && o + 1 < outLen) {
    char c = raw[i++];
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    out[o++] = c;
  }
  out[o] = '\0';
  return o > 0;
}

bool applyCommand(const char* raw, size_t length) {
  char cmd[8];
  if (!normalize(raw, length, cmd, sizeof(cmd))) {
    return false;
  }
  if (strcmp(cmd, "open") == 0) {
    requestMove(true);
    return true;
  }
  if (strcmp(cmd, "close") == 0) {
    requestMove(false);
    return true;
  }
  if (strcmp(cmd, "stop") == 0) {
    requestStop();
    return true;
  }
  return false;
}

void applyPending() {
  if (!pending) {
    return;
  }
  pending = false;
  applyCommand(pendingCmd, strlen(pendingCmd));
}

void motorTimers(uint32_t now) {
  if (!moving()) {
    return;
  }
  if (phase == Phase::Settling) {
    if (static_cast<int32_t>(now - settleUntil) >= 0) {
      writePower(true);
      phase = Phase::Running;
      poweredAt = now;
      endstopPrimed = false;
    }
    return;
  }

  const uint16_t seconds = motion == Motion::Opening ? openSeconds : closeSeconds;
  const uint32_t limitMs = static_cast<uint32_t>(seconds) * 1000UL;
  if (static_cast<uint32_t>(now - poweredAt) >= limitMs) {
    finishMove(Motion::Stopped, motion == Motion::Opening ? "open, travel time" : "close, travel time");
  }
}

void updateEndstop(float absAmps, uint32_t now) {
  if (!endstopPrimed || ENDSTOP_TAU_S <= 0.0f) {
    endstopAmps = absAmps;
    endstopPrimed = true;
    lastEndstopMs = now;
    return;
  }

  float dt = static_cast<float>(now - lastEndstopMs) / 1000.0f;
  lastEndstopMs = now;
  if (dt < 0.0f) {
    dt = 0.0f;
  } else if (dt > 1.0f) {
    dt = 1.0f;
  }
  const float alpha = 1.0f - expf(-dt / ENDSTOP_TAU_S);
  endstopAmps += alpha * (absAmps - endstopAmps);
}

void onMqtt(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, MQTT_COMMAND_TOPIC) != 0) {
    return;
  }
  char cmd[8];
  if (!normalize(reinterpret_cast<const char*>(payload), length, cmd, sizeof(cmd))) {
    return;
  }
  strncpy(pendingCmd, cmd, sizeof(pendingCmd) - 1);
  pendingCmd[sizeof(pendingCmd) - 1] = '\0';
  pending = true;
}

void publishState() {
  if (!mqtt.connected() || !stateDirty) {
    return;
  }
  if (mqtt.publish(MQTT_STATE_TOPIC, motionName(motion), true)) {
    stateDirty = false;
  }
}

void connectMqtt() {
  wifiClient.setConnectionTimeout(2000);
  bool ok;
  if (MQTT_USER[0] == '\0') {
    ok = mqtt.connect(MQTT_CLIENT_ID, MQTT_AVAIL_TOPIC, 1, true, "offline");
  } else {
    ok = mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD, MQTT_AVAIL_TOPIC, 1, true,
                      "offline");
  }
  nextMqttAt = millis() + 5000;
  if (!ok) {
    Serial.println("MQTT connect failed");
    return;
  }

  Serial.println("MQTT connected");
  mqtt.publish(MQTT_AVAIL_TOPIC, "online", true);
  if (!mqtt.publish(MQTT_DISCOVERY_TOPIC, DISCOVERY_JSON, true)) {
    Serial.println("MQTT discovery publish failed");
  }
  stateDirty = true;
  publishState();
  mqtt.subscribe(MQTT_COMMAND_TOPIC, 1);
}

void mqttPump(bool allowReconnect) {
  if (!mqttConfigured || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (mqtt.connected()) {
    mqtt.loop();
    applyPending();
    publishState();
    return;
  }
  // connect() blocks, so a moving motor keeps its end-stop checks instead.
  if (!allowReconnect || moving()) {
    return;
  }
  if (static_cast<int32_t>(millis() - nextMqttAt) < 0) {
    return;
  }
  connectMqtt();
}

}  // namespace

void shutterBegin() {
  driveOutput(PIN_MOTOR_POWER, powerLevel(false));
  driveOutput(PIN_MOTOR_DIR, directionLevel(false));
  loadSettings();

  mqttConfigured = MQTT_HOST[0] != '\0';
  if (!mqttConfigured) {
    return;
  }
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqtt);
  mqtt.setSocketTimeout(2);
  mqtt.setBufferSize(1024);
}

void shutterLogConfig() {
  Serial.print("threshold ");
  Serial.print(thresholdA, 2);
  Serial.print(" A, open ");
  Serial.print(openSeconds);
  Serial.print(" s, close ");
  Serial.print(closeSeconds);
  Serial.println(" s");
  if (!mqttConfigured) {
    Serial.println("MQTT disabled (empty MQTT_HOST)");
  }
}

void shutterService(bool allowReconnect) {
  applyPending();
  motorTimers(millis());
  mqttPump(allowReconnect);
}

void shutterOnCurrent(float absAmps, uint32_t now) {
  if (absAmps < 0.0f) {
    absAmps = -absAmps;
  }
  updateEndstop(absAmps, now);
  if (phase != Phase::Running || !moving()) {
    return;
  }
  if (static_cast<uint32_t>(now - poweredAt) < MOTOR_BLANKING_MS) {
    return;
  }
  if (endstopAmps < thresholdA) {
    return;
  }
  finishMove(motion == Motion::Opening ? Motion::Open : Motion::Closed,
             motion == Motion::Opening ? "open, current end-stop" : "close, current end-stop");
}

const char* shutterState() {
  return motionName(motion);
}

bool shutterCommand(const char* cmd) {
  if (cmd == nullptr) {
    return false;
  }
  return applyCommand(cmd, strlen(cmd));
}

bool shutterSaveSettings(float newThreshold, long newOpenSeconds, long newCloseSeconds) {
  if (!thresholdOk(newThreshold) || !secondsOk(newOpenSeconds) || !secondsOk(newCloseSeconds)) {
    return false;
  }
  if (!prefsOpen) {
    prefsOpen = prefs.begin("shutter", false);
  }
  if (!prefsOpen) {
    return false;
  }
  thresholdA = newThreshold;
  openSeconds = static_cast<uint16_t>(newOpenSeconds);
  closeSeconds = static_cast<uint16_t>(newCloseSeconds);
  prefs.putFloat("thr", thresholdA);
  prefs.putUShort("open", openSeconds);
  prefs.putUShort("close", closeSeconds);
  Serial.print("settings saved, threshold ");
  Serial.print(thresholdA, 2);
  Serial.print(" A, open ");
  Serial.print(openSeconds);
  Serial.print(" s, close ");
  Serial.print(closeSeconds);
  Serial.println(" s");
  return true;
}

int shutterWriteSettingsJson(char* buf, size_t length) {
  float shown = thresholdA;
  if (shown < 0.0f) {
    shown = 0.0f;
  }
  const int scaled = static_cast<int>(shown * 1000.0f + 0.5f);
  return snprintf(buf, length,
                  "{\"threshold\":%d.%03d,\"openSeconds\":%u,\"closeSeconds\":%u,\"state\":\"%s\"}",
                  scaled / 1000, scaled % 1000, openSeconds, closeSeconds, motionName(motion));
}
