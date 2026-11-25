# ESP32-C6-Relay-1CH-U ESP-IDF Demo

[简体中文](README_ZH.md)

## What it does

Runs a relay control demo with Wi-Fi AP + Web and Wi-Fi STA + Web modes. The web interface also exposes RTC and peripheral tests.

## Hardware

ESP32-C6-Relay-1CH-U and a USB Type-C data cable.

## How to run

Use ESP-IDF **6.1.0** (release tag `v6.1`). Run the following commands in this directory and replace `PORT` with your serial port.

```sh
idf.py set-target esp32c6
idf.py build
idf.py -p PORT flash monitor
```

To produce a single image for Flash Download Tool, run `idf.py merge-bin --format raw --flash-offset 0x0`. Select ESP32-C6 and write `build/merged-binary.bin` at `0x00` with **DoNotChgBin** enabled.

## Expected result

In the default AP mode, connect to `ESP32-C6-Relay-1CH-U` with password `waveshare` and open `http://192.168.4.1/` to control the relay. In STA mode, connect your browser to the same network and open the IP address printed as `STA IP=` in the serial monitor.

## FAQ

To use STA mode, open `idf.py menuconfig` after setting the target. In the demo configuration menu, choose **Wi-Fi STA + Web** and enter your router SSID and password before building.
