# ESP-IDF Relay Demo Firmware

[简体中文](README_ZH.md)

The [ESP-IDF example](../examples/esp-idf/ESP32-C6-Relay-1CH-U/) is configured for ESP-IDF **6.1.0** (release tag `v6.1`), the version used to build the latest BIN. The merged BIN includes the bootloader, partition table and the AP demo application with an English web interface.

| File | Target | ESP-IDF | Write address |
| --- | --- | --- | --- |
| `ESP32-C6-Relay-1CH-U_FactoryOnly_20261126.bin` | ESP32-C6 | 6.1.0 | `0x00` |

The image uses DIO at 80 MHz with a 2 MB flash layout. The bootloader is at `0x0` within the merged BIN.

To build the BIN, run these commands from the ESP-IDF example directory. The file is created in `build/`.

```sh
idf.py set-target esp32c6
idf.py build
idf.py merge-bin --format raw --flash-offset 0x0 -o ESP32-C6-Relay-1CH-U_FactoryOnly_20261126.bin
```

In Flash Download Tool, select the chip shown above, enter `0x00` for the merged BIN, and enable **DoNotChgBin** to keep the flash settings in the image. With esptool, replace `PORT` and run the command from the folder containing the BIN.

```sh
python -m esptool --chip esp32c6 --port PORT write-flash 0x00 ESP32-C6-Relay-1CH-U_FactoryOnly_20261126.bin
```

In the default AP mode, connect to `ESP32-C6-Relay-1CH-U` with password `12345678` and open `http://192.168.4.1/` to control the relay.
