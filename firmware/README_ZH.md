# ESP-IDF 继电器示例固件

[English](README.md)

[ESP-IDF 示例](../examples/esp-idf/ESP32-C6-Relay-1CH-U/README_ZH.md)指定 ESP-IDF **6.1.0**（官方发布标签为 `v6.1`），最新 BIN 也使用该版本编译。合并 BIN 包含 bootloader、分区表和 AP 演示应用，内置网页为英文。

| 文件 | 芯片 | ESP-IDF | 烧录地址 |
| --- | --- | --- | --- |
| `ESP32-C6-Relay-1CH-U_FactoryOnly_20261126.bin` | ESP32-C6 | 6.1.0 | `0x00` |

镜像采用 DIO、80 MHz 和 2 MB Flash 布局。引导程序在合并 BIN 内的偏移为 `0x0`。

在 ESP-IDF 示例目录执行以下命令，生成的合并 BIN 位于 `build/`。

```sh
idf.py set-target esp32c6
idf.py build
idf.py merge-bin --format raw --flash-offset 0x0 -o ESP32-C6-Relay-1CH-U_FactoryOnly_20261126.bin
```

使用 Flash Download Tool 时选择表中的芯片型号，将合并 BIN 的地址填写为 `0x00`，并勾选 **DoNotChgBin** 以保留镜像内的 Flash 参数。使用 esptool 时，在 BIN 所在目录运行以下命令，并替换 `PORT`。

```sh
python -m esptool --chip esp32c6 --port PORT write-flash 0x00 ESP32-C6-Relay-1CH-U_FactoryOnly_20261126.bin
```

默认 AP 模式下，连接 `ESP32-C6-Relay-1CH-U` 热点，密码为 `12345678`。打开 `http://192.168.4.1/` 控制继电器。
