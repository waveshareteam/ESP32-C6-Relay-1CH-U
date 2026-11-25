# MAIN_WIFI_AP

[English](README.md)

## 功能

Wi-Fi AP 网页控制。

## 硬件需求

ESP32-C6-Relay-1CH-U 和 USB Type-C 数据线。

## 怎么运行

安装 Arduino-ESP32 **4.0.0**，将[随附库](../../libraries/)加入 Arduino 草图本。打开 `MAIN_WIFI_AP.ino`，选择 **ESP32C6 Dev Module**，启用 **USB CDC On Boot**，将 **Partition Scheme** 设为 **No OTA (2MB APP/2MB SPIFFS)**，然后上传。

## 预期现象

连接热点后打开 `http://192.168.4.1/`，可在网页控制继电器。

## 常见问题

连接 `ESP32-C6-Relay-1CH-U` 热点，密码为 `waveshare`。
