# ESP32-C6-Relay-1CH-U ESP-IDF 示例

[English](README.md)

## 功能

运行继电器控制示例，可选择 Wi-Fi AP + Web 或 Wi-Fi STA + Web。网页还提供 RTC 和外设测试。

## 硬件需求

ESP32-C6-Relay-1CH-U 和 USB Type-C 数据线。

## 怎么运行

使用 ESP-IDF **6.1.0**（官方发布标签为 `v6.1`）。在本目录执行以下命令，将 `PORT` 替换为实际串口。

```sh
idf.py set-target esp32c6
idf.py build
idf.py -p PORT flash monitor
```

运行 `idf.py merge-bin --format raw --flash-offset 0x0` 可生成单个合并 BIN。在 Flash Download Tool 中选 ESP32-C6，把 `build/merged-binary.bin` 写入 `0x00`，并勾选 **DoNotChgBin**。

## 预期现象

默认 AP 模式下，连接 `ESP32-C6-Relay-1CH-U` 热点，密码为 `waveshare`。打开 `http://192.168.4.1/` 控制继电器。STA 模式下，让浏览器所在设备接入同一网络，访问串口日志 `STA IP=` 显示的地址。

## 常见问题

使用 STA 模式时，先设置目标芯片，再运行 `idf.py menuconfig`。在演示配置菜单中选择 **Wi-Fi STA + Web**，填写路由器 SSID 和密码后编译。
