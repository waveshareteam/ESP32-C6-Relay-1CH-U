#include "WS_MQTT.h"

// Details about devices on the Waveshare cloud
const char* mqtt_server = MQTT_Server;
int PORT = MQTT_Port;
const char* ID = MQTT_ID;   // Defining device ID
char pub[] = MQTT_Pub;      // MQTT release topic
char sub[] = MQTT_Sub;      // MQTT subscribe to topics


WiFiClient espClient;       //MQTT initializes the contents
PubSubClient client(espClient);

JsonDocument sendJson;
char msg[MSG_BUFFER_SIZE];

void callback(char *, byte *payload, unsigned int length) {
  JsonDocument message;
  if (deserializeJson(message, payload, length)) {
    return;
  }

  JsonObjectConst data = message["data"].as<JsonObjectConst>();
  JsonVariantConst state = data["CH1"];
  if (state.isNull()) {
    state = data["ALL"];
  }
  if (!state.is<int>()) {
    return;
  }
  const int value = state.as<int>();
  if (value != 0 && value != 1) {
    return;
  }
  uint8_t command = value == 1 ? CH1_ON : CH1_OFF;
  Relay_Analysis(&command, MQTT_Mode);
}

void reconnect(void) {
  if (client.connect(ID)) {
    if (client.subscribe(sub)) {
      printf("MQTT connected.\n");
    } else {
      client.disconnect();
      printf("MQTT subscription failed.\n");
    }
  } else {
    printf("MQTT connection failed: %d\n", client.state());
  }
}

// Send data in JSON format to MQTT server
void sendJsonData(void) {
  sendJson["ID"] = ID;
  String pubres;
  serializeJson(sendJson, pubres);
  int str_len = pubres.length() + 1;
  char char_array[str_len];
  pubres.toCharArray(char_array, str_len);
  client.publish(pub, char_array);
}

void MQTTTask(void *) {
  constexpr uint32_t retryInterval = 3000;
  uint32_t lastAttempt = millis() - retryInterval;
  while (true) {
    if (WiFi.status() == WL_CONNECTED) {
      if (client.connected()) {
        client.loop();
      } else if (millis() - lastAttempt >= retryInterval) {
        reconnect();
        lastAttempt = millis();
      }
    } else if (client.connected()) {
      client.disconnect();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void MQTT_Init(void)
{
  client.setServer(mqtt_server, PORT);
  client.setCallback(callback);
  client.setSocketTimeout(3);
  WIFI_Init();
  xTaskCreatePinnedToCore(
    MQTTTask,    
    "MQTTTask",   
    4096,                
    NULL,                 
    3,                   
    NULL,                 
    0                   
  );
}

