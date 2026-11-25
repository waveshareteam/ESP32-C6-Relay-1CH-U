# MAIN_ALL

[English](README.md)

## 功能

Wi-Fi STA 网页控制与 MQTT。

## 硬件需求

ESP32-C6-Relay-1CH-U 和 USB Type-C 数据线。

## 怎么运行

安装 Arduino-ESP32 **4.0.0**，将[随附库](../../libraries/)加入 Arduino 草图本。打开 `MAIN_ALL.ino`，选择 **ESP32C6 Dev Module**，启用 **USB CDC On Boot**，将 **Partition Scheme** 设为 **No OTA (2MB APP/2MB SPIFFS)**，然后上传。

## 预期现象

联网后，通过串口输出的 IP 打开网页控制继电器，也可通过配置的 MQTT 主题控制。

## 常见问题

使用 STA 时，在 `WS_Information.h` 填写路由器 SSID 和密码。 在同一文件填写 MQTT 服务器、客户端 ID 和主题。
