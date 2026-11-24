# MAX471 current graph

PlatformIO firmware for the Waveshare ESP32-C6 Zero. It samples a MAX471 current sensor and serves a live chart after joining a 2.4 GHz Wi-Fi network.

The ESP32-C6 radio is 2.4 GHz only. Open `http://max471.local/` (or the IP address printed on the serial monitor) from another device on the same network.

## Wiring

MAX471 OUT is a current output, 500 µA per amp of load current. A 2 kΩ resistor from OUT to GND turns that into 1 V/A, full scale about ±3 A (3 V). Breakout boards usually include that resistor on the OUT or AT pin. A bare MAX471 needs the 2 kΩ fitted before OUT is connected to a GPIO.

RS+ and the load supply stay off the ESP32. Those pins can be as high as 36 V. Share ground between the sensor and the board.

| MAX471 | ESP32-C6 Zero |
| --- | --- |
| OUT or AT | GPIO0 (A0) |
| SIGN | GPIO1 |
| GND | GND |

SIGN is open-collector. GPIO1 uses its internal pull-up. A low SIGN means current is flowing from RS− toward RS+, and the chart shows that as a negative current. If the sign is backwards for your wiring, set `SIGN_LOW_IS_REVERSE` to `false` in [include/config.h](include/config.h).

Leave GPIO8 for the onboard RGB LED, GPIO9 for BOOT, and GPIO12/GPIO13 for USB.

If the breakout's VT pin is used instead of AT, set `VOLTS_PER_AMP` to `0.2` in `include/config.h`. `ZERO_MV` is subtracted from the OUT reading before the scale is applied.

## Wi-Fi

```powershell
copy include\secrets.h.example include\secrets.h
```

Edit `include/secrets.h` and set `WIFI_SSID` and `WIFI_PASSWORD`. That file is gitignored.

The pioarduino stable package used here does not ship a Waveshare board manifest. [boards/waveshare_esp32_c6_zero.json](boards/waveshare_esp32_c6_zero.json) supplies it and selects the `waveshare_esp32_c6_zero` pin variant from the Arduino core, with 4 MB flash.

## Build and flash

```powershell
pio run
pio run -t upload
pio device monitor
```

The serial monitor is 115200 baud. The onboard LED is yellow while connecting, green while the page is being served, and red after repeated join failures.

The chart page polls `/api/reading` and keeps the last 60 seconds. Each line on the serial monitor is one 10 Hz sample: milliseconds, signed amps, OUT millivolts, and direction.
