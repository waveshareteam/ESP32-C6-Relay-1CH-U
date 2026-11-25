#include "WS_Bluetooth.h"

#include <BLE.h>

static BLEServer server;
static BLECharacteristic txCharacteristic;

static void BLE_Set_RTC_Event(const uint8_t *valueBytes);

static void receiveCommand(BLECharacteristic &characteristic, const BLEConnInfo &) {
  size_t length = 0;
  const uint8_t *value = characteristic.getValue(&length);
  if (length == 1) {
    uint8_t command = value[0];
    Relay_Analysis(&command, Bluetooth_Mode);
  } else if (length == 2 && Extension_Enable && value[0] == 0x06) {
    uint8_t command[] = {value[0], value[1]};
    RS485_Analysis(command);
  } else if (length == 14 && RTC_Event_Enable) {
    BLE_Set_RTC_Event(value);
  } else if (length != 0) {
    printf("Invalid Bluetooth command.\n");
  }
}

static uint8_t decodeBcd(uint8_t value) {
  return (value >> 4) * 10 + (value & 0x0F);
}

static void BLE_Set_RTC_Event(const uint8_t *value) {
  if (value[0] != 0xA1 || value[6] != 0xAA || value[13] != 0xFF ||
      value[11] > 1 || value[12] > Repetition_monthly) {
    return;
  }
  for (const uint8_t index : {1, 2, 3, 4, 5, 7, 8, 9}) {
    if ((value[index] >> 4) > 9 || (value[index] & 0x0F) > 9) {
      return;
    }
  }

  // The single-channel command uses a zero-based channel number.
  if (value[11] == 0 && (value[10] >> 4) != 0) {
    return;
  }
  const bool on = value[11] == 1 ? (value[10] & 1) != 0 : (value[10] & 0x0F) != 0;
  const datetime_t time = {
    static_cast<uint16_t>(decodeBcd(value[1]) * 100 + decodeBcd(value[2])),
    decodeBcd(value[3]), decodeBcd(value[4]), decodeBcd(value[5]),
    decodeBcd(value[7]), decodeBcd(value[8]), decodeBcd(value[9]),
  };
  if (!TimerEvent_Add(time, on ? STATE_Open : STATE_Closs, static_cast<Repetition_event>(value[12]))) {
    printf("Invalid RTC event or event list full.\n");
  }
}

static void BLETask(void *) {
  String lastAddress;
  while (true) {
    String address = WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString()
                                             : WiFi.localIP().toString();
    if (address != lastAddress) {
      lastAddress = address;
      txCharacteristic.setValue(address);
    }
    if (txCharacteristic.getSubscribedCount() > 0) {
      txCharacteristic.notify();
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void Bluetooth_Init() {
  BTStatus status = BLE.begin(RELAY_DEVICE_NAME);
  if (!status) {
    printf("BLE initialization failed: %s\n", status.toString());
    return;
  }

  server = BLE.createServer();
  if (!server) {
    printf("BLE server creation failed.\n");
    return;
  }
  BLEService service = server.createService(SERVICE_UUID);
  if (!service) {
    printf("BLE service creation failed.\n");
    return;
  }

  txCharacteristic = service.createCharacteristic(
    TX_CHARACTERISTIC_UUID, BLEProperty::Read | BLEProperty::Notify, BLEPermission::ReadOpen);
  BLECharacteristic rxCharacteristic = service.createCharacteristic(
    RX_CHARACTERISTIC_UUID, BLEProperty::Write, BLEPermission::WriteOpen);
  if (!txCharacteristic || !rxCharacteristic) {
    printf("BLE characteristic creation failed.\n");
    return;
  }
  rxCharacteristic.onWrite(receiveCommand);
  server.advertiseOnDisconnect(true);

  status = server.start();
  if (!status) {
    printf("BLE server start failed: %s\n", status.toString());
    return;
  }

  BLEAdvertising advertising = BLE.getAdvertising();
  advertising.addServiceUUID(SERVICE_UUID);
  advertising.setScanResponse(true);
  status = advertising.start();
  if (!status) {
    printf("BLE advertising failed: %s\n", status.toString());
    return;
  }
  printf("BLE device: %s\n", RELAY_DEVICE_NAME);
  xTaskCreatePinnedToCore(BLETask, "BLETask", 4096, nullptr, 2, nullptr, 0);
}
