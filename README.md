# Persiana controller

PlatformIO firmware for a Waveshare ESP32-C6 Zero. It drives a shutter motor through two relays, stops on the MAX471 current rise at the bumper, and stops anyway when the travel time runs out. The same open, close, and stop commands are on the web page and on a Home Assistant MQTT cover.

The ESP32-C6 radio is 2.4 GHz only. Open `http://persianas.local/` (or the IP address printed on the serial monitor) from another device on the same network.

## Wiring

MAX471 OUT is a current output, 500 µA per amp of load current. A 2 kΩ resistor from OUT to GND turns that into 1 V/A, full scale about ±3 A (3 V). Breakout boards usually include that resistor on the OUT or AT pin. A bare MAX471 needs the 2 kΩ fitted before OUT is connected to a GPIO.

RS+ and the motor supply stay off the ESP32. Those pins can be as high as 36 V. Put the MAX471 in series with the motor supply, before the direction relay, and share ground between the sensor, the relay module, and the board.

| Signal | ESP32-C6 Zero |
| --- | --- |
| MAX471 OUT or AT | GPIO0 (A0) |
| MAX471 SIGN | GPIO1 |
| MAX471 GND | GND |
| Power relay input | GPIO2 |
| Direction relay input | GPIO3 |

The power relay switches the motor supply. The direction relay swaps the motor leads. The ESP32 only drives the relay inputs. Leave GPIO8 for the onboard RGB LED, GPIO9 for BOOT, and GPIO12/GPIO13 for USB.

SIGN is open-collector. GPIO1 uses its internal pull-up. A low SIGN means current is flowing from RS− toward RS+, and the chart shows that as a negative current. The end-stop uses the absolute current, so the sign does not matter. If the chart sign is backwards, set `SIGN_LOW_IS_REVERSE` to `false` in [include/config.h](include/config.h).

If the breakout's VT pin is used instead of AT, set `VOLTS_PER_AMP` to `0.2`. `ZERO_MV` is subtracted from the OUT reading before the scale is applied.

Relay modules are usually active-low: a high GPIO leaves the relay off. `RELAY_ACTIVE_LOW` in `include/config.h` defaults to that. The power output is driven off before Wi-Fi starts. `DIRECTION_OPEN_HIGH` chooses which direction-pin level means open.

## End-stop

The motor current rises when the shutter reaches the bumper. After the first second of a move (so the startup spike is ignored), the motor stops when the filtered current reaches the saved threshold. Opening then reports `open`. Closing reports `closed`.

The threshold has to sit above the normal running current and below the bumper current. The orange lines on the chart are that threshold. The default is 1.0 A, and the page accepts 0.05 A to 3.0 A.

If the current never reaches the threshold, the motor still stops at the saved travel time. That stop reports `stopped`, because the position was not confirmed. Open and close have separate times, from 1 to 180 seconds, default 25. After a reboot the position is unknown until the next current end-stop.

## Wi-Fi and MQTT

```powershell
copy include\secrets.h.example include\secrets.h
```

Edit `include/secrets.h`. Set `WIFI_SSID`, `WIFI_PASSWORD`, `MQTT_HOST`, `MQTT_PORT`, `MQTT_USER`, and `MQTT_PASSWORD`. An empty `MQTT_HOST` skips MQTT. The file is gitignored.

On connect the board publishes a retained Home Assistant discovery document for a shutter cover, then the retained state.

| Topic | Payload |
| --- | --- |
| `persianas/cover/set` | `OPEN`, `CLOSE`, `STOP` |
| `persianas/cover/state` | `opening`, `closing`, `open`, `closed`, `stopped` |
| `persianas/status` | `online` / `offline` |
| `homeassistant/cover/persianas/config` | discovery |

Home Assistant shows the cover as unavailable when the board drops. A command in the direction already running, or at an end that was confirmed by current, is ignored. The opposite command reverses the motor. Stop while the shutter is already open or closed leaves that position as it is.

The pioarduino stable package used here does not ship a Waveshare board manifest. [boards/waveshare_esp32_c6_zero.json](boards/waveshare_esp32_c6_zero.json) supplies it and selects the `waveshare_esp32_c6_zero` pin variant from the Arduino core, with 4 MB flash.

## Build and flash

```powershell
pio run
pio run -t upload
pio device monitor
```

The serial monitor is 115200 baud. The onboard LED is yellow while connecting, green while the page is being served, and red after repeated join failures.

The page polls `/api/reading` and keeps the last 60 seconds. Each serial line is one 10 Hz sample: milliseconds, signed amps, OUT millivolts, current direction, and the shutter state.
