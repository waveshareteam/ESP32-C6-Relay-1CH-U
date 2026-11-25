# MAIN_WIFI_STA

[简体中文](README_ZH.md)

## What it does

Wi-Fi STA web control.

## Hardware

ESP32-C6-Relay-1CH-U and a USB Type-C data cable.

## How to run

Install Arduino-ESP32 **4.0.0** and add the [bundled libraries](../../libraries/) to your Arduino sketchbook. Open `MAIN_WIFI_STA.ino`, select **ESP32C6 Dev Module**, enable **USB CDC On Boot**, set **Partition Scheme** to **No OTA (2MB APP/2MB SPIFFS)**, and upload.

## Expected result

After joining Wi-Fi, open the IP shown in the serial monitor to control the relay.

## FAQ

For STA, set the router credentials in `WS_Information.h`.
