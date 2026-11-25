#pragma once

#define RELAY_DEVICE_NAME "ESP32-C6-Relay-1CH-U"

#define Extension_Enable      1                   // Whether to extend the connection to external devices     1:Expansion device Modbus RTU Relay     0:No extend
#define RTC_Event_Enable      1                   // Whether to enable RTC events  (Bluetooth)                1:Enable                                0:Disable



// Name and password of the WiFi access point
#define STASSID       "YOUR_WIFI_SSID"
#define STAPSK        "YOUR_WIFI_PASSWORD"

// Details about devices on the Waveshare cloud
#define MQTT_Server   "mqtt.example.invalid"
#define MQTT_Port     1883
#define MQTT_ID       "your-client-id"
#define MQTT_Pub      "relay/demo/status"
#define MQTT_Sub      "relay/demo/set"
