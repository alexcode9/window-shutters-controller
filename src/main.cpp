#include <Arduino.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include <cmath>

#include "config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing include/secrets.h. Copy include/secrets.h.example to include/secrets.h and set WIFI_SSID and WIFI_PASSWORD."
#endif

#include "web_page.h"

static WebServer server(HTTP_PORT);

struct Reading {
  uint32_t ms;
  float amps;
  float mV;
  bool reverse;
};

static Reading latest = {0, 0.0f, 0.0f, false};
static uint32_t nextSampleAt = 0;
static uint32_t nextWifiAt = 0;
static uint8_t wifiAttempts = 0;
static bool serverUp = false;
static bool wasConnected = false;

static void setLed(uint8_t r, uint8_t g, uint8_t b) {
  static uint8_t lastR = 255;
  static uint8_t lastG = 255;
  static uint8_t lastB = 255;
  if (r == lastR && g == lastG && b == lastB) {
    return;
  }
  lastR = r;
  lastG = g;
  lastB = b;
  rgbLedWrite(PIN_RGB_LED, r, g, b);
}

static void showWifiLed() {
  if (wasConnected && serverUp) {
    setLed(0, LED_LEVEL, 0);
    return;
  }
  if (wifiAttempts >= 5) {
    setLed(LED_LEVEL, 0, 0);
    return;
  }
  setLed(LED_LEVEL, LED_LEVEL, 0);
}

static float averagedMilliVolts() {
  uint32_t sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += analogReadMilliVolts(PIN_OUT);
  }
  return static_cast<float>(sum) / static_cast<float>(ADC_SAMPLES);
}

static float filteredAmps = 0.0f;
static float filteredMv = 0.0f;
static bool filterPrimed = false;
static uint32_t lastFilterMs = 0;

static void lowPass(float amps, float millivolts, uint32_t now) {
  if (!filterPrimed || FILTER_TAU_S <= 0.0f) {
    filteredAmps = amps;
    filteredMv = millivolts;
    filterPrimed = true;
    lastFilterMs = now;
    return;
  }

  float dt = static_cast<float>(now - lastFilterMs) / 1000.0f;
  lastFilterMs = now;
  if (dt < 0.0f) {
    dt = 0.0f;
  } else if (dt > 1.0f) {
    dt = 1.0f;
  }
  const float alpha = 1.0f - expf(-dt / FILTER_TAU_S);
  filteredAmps += alpha * (amps - filteredAmps);
  filteredMv += alpha * (millivolts - filteredMv);
}

static void sampleSensor() {
  const float measuredMv = averagedMilliVolts();
  float magnitudeMv = measuredMv - ZERO_MV;
  if (magnitudeMv < 0.0f) {
    magnitudeMv = 0.0f;
  }

  float amps = (magnitudeMv / 1000.0f) / VOLTS_PER_AMP;
  const bool signLow = digitalRead(PIN_SIGN) == LOW;
  const bool reverse = SIGN_LOW_IS_REVERSE ? signLow : !signLow;
  if (reverse) {
    amps = -amps;
  }

  const uint32_t now = millis();
  lowPass(amps, measuredMv, now);

  latest.ms = now;
  latest.amps = filteredAmps;
  latest.mV = filteredMv;
  latest.reverse = reverse;

  Serial.printf("%lu ms  ", static_cast<unsigned long>(latest.ms));
  Serial.print(latest.amps, 3);
  Serial.print(" A  ");
  Serial.print(latest.mV, 0);
  Serial.print(" mV  ");
  Serial.println(latest.reverse ? "reverse" : "forward");
}

// Formats one JSON reading without relying on printf float support.
static void formatReading(char* body, size_t length) {
  const bool negative = latest.amps < 0.0f;
  float magnitude = negative ? -latest.amps : latest.amps;
  int scaled = static_cast<int>(magnitude * 10000.0f + 0.5f);
  const int whole = scaled / 10000;
  const int frac = scaled % 10000;
  int millivolts = static_cast<int>(latest.mV + 0.5f);
  if (millivolts < 0) {
    millivolts = 0;
  }

  snprintf(body, length,
           "{\"ms\":%lu,\"amps\":%s%d.%04d,\"mV\":%d,\"reverse\":%s}",
           static_cast<unsigned long>(latest.ms),
           negative ? "-" : "",
           whole,
           frac,
           millivolts,
           latest.reverse ? "true" : "false");
}

static void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

static void handleReading() {
  char body[96];
  formatReading(body, sizeof(body));
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}

static void handleFavicon() {
  server.send(204);
}

static void startServer() {
  if (serverUp) {
    return;
  }
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/reading", HTTP_GET, handleReading);
  server.on("/favicon.ico", HTTP_GET, handleFavicon);
  server.begin();
  serverUp = true;
}

static void announce() {
  Serial.print("Chart: http://");
  Serial.print(WiFi.localIP());
  Serial.print("/  http://");
  Serial.print(MDNS_HOST);
  Serial.println(".local/");
}

// Association plus DHCP often takes longer than a couple of seconds.
// Retrying sooner aborts a join that is still in progress.
static uint32_t wifiBackoffMs() {
  uint32_t waitMs = 10000UL * wifiAttempts;
  if (waitMs > 30000) {
    waitMs = 30000;
  }
  return waitMs;
}

static void connectWifi() {
  WiFi.disconnect(false, false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  if (wifiAttempts < 255) {
    wifiAttempts++;
  }
  const uint32_t waitMs = wifiBackoffMs();
  nextWifiAt = millis() + waitMs;
  Serial.printf("Wi-Fi attempt %u, next try in %lu ms\n", wifiAttempts,
                static_cast<unsigned long>(waitMs));
}

static void serviceWifi() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    if (!wasConnected) {
      wifiAttempts = 0;
      startServer();
      if (MDNS.begin(MDNS_HOST)) {
        MDNS.addService("http", "tcp", HTTP_PORT);
      } else {
        Serial.println("mDNS start failed");
      }
      announce();
      wasConnected = true;
    }
    return;
  }

  if (wasConnected) {
    MDNS.end();
    wasConnected = false;
    Serial.println("Wi-Fi lost");
  }

  if (static_cast<int32_t>(millis() - nextWifiAt) >= 0) {
    connectWifi();
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("MAX471 current graph");

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_OUT, ADC_11db);
  pinMode(PIN_SIGN, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_HOST);
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);

  setLed(LED_LEVEL, LED_LEVEL, 0);
  connectWifi();
  nextSampleAt = millis();
}

void loop() {
  serviceWifi();
  showWifiLed();
  if (serverUp) {
    server.handleClient();
  }

  if (static_cast<int32_t>(millis() - nextSampleAt) >= 0) {
    sampleSensor();
    nextSampleAt += 1000 / SAMPLE_HZ;
    if (static_cast<int32_t>(millis() - nextSampleAt) >= 0) {
      nextSampleAt = millis() + (1000 / SAMPLE_HZ);
    }
  }
}
