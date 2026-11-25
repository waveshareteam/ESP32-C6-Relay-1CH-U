# MAIN_WIFI_MQTT

[简体中文](README_ZH.md)

## What it does

MQTT control over Wi-Fi STA.

## Hardware

ESP32-C6-Relay-1CH-U and a USB Type-C data cable.

## How to run

Install Arduino-ESP32 **4.0.0** and add the [bundled libraries](../../libraries/) to your Arduino sketchbook. Open `MAIN_WIFI_MQTT.ino`, select **ESP32C6 Dev Module**, enable **USB CDC On Boot**, set **Partition Scheme** to **No OTA (2MB APP/2MB SPIFFS)**, and upload.

## Expected result

After Wi-Fi and MQTT connect, use the configured MQTT topics to operate the relay.

## FAQ

For STA, set the router credentials in `WS_Information.h`. Set the MQTT broker, client ID and topics in the same file.
