#include "WS_RS485.h"

HardwareSerial lidarSerial(1);  // Using serial port 1
static const struct {
  uint8_t frame[8];
  uint8_t action;
} relayCommands[] = {
  { { 0x06, 0x05, 0x00, 0x01, 0x55, 0x00, 0xA2, 0xED }, CH1 },
  { { 0x06, 0x05, 0x00, 0xFF, 0xFF, 0x00, 0xBD, 0xBD }, CH1_ON },
  { { 0x06, 0x05, 0x00, 0xFF, 0x00, 0x00, 0xFC, 0x4D }, CH1_OFF },
};
uint8_t Send_Data[][8] = {                                  // Modbus RTU Relay Control Command (RS485 send data)
  { 0x01, 0x05, 0x00, 0x00, 0x55, 0x00, 0xF2, 0x9A },       // Modbus RTU Relay CH1 Toggle
  { 0x01, 0x05, 0x00, 0x01, 0x55, 0x00, 0xA3, 0x5A },       // Modbus RTU Relay CH2 Toggle
  { 0x01, 0x05, 0x00, 0x02, 0x55, 0x00, 0x53, 0x5A },       // Modbus RTU Relay CH3 Toggle
  { 0x01, 0x05, 0x00, 0x03, 0x55, 0x00, 0x02, 0x9A },       // Modbus RTU Relay CH4 Toggle
  { 0x01, 0x05, 0x00, 0x04, 0x55, 0x00, 0xB3, 0x5B },       // Modbus RTU Relay CH5 Toggle
  { 0x01, 0x05, 0x00, 0x05, 0x55, 0x00, 0xE2, 0x9B },       // Modbus RTU Relay CH6 Toggle
  { 0x01, 0x05, 0x00, 0x06, 0x55, 0x00, 0x12, 0x9B },       // Modbus RTU Relay CH7 Toggle
  { 0x01, 0x05, 0x00, 0x07, 0x55, 0x00, 0x43, 0x5B },       // Modbus RTU Relay CH8 Toggle
  { 0x01, 0x05, 0x00, 0xFF, 0xFF, 0xFF, 0xFC, 0x4A },       // Modbus RTU Relay ALL ON
  { 0x01, 0x05, 0x00, 0xFF, 0x00, 0x00, 0xFD, 0xFA },       // Modbus RTU Relay ALL OFF
};
void SetData(uint8_t* data, size_t length) {
  lidarSerial.write(data, length);                          // Send data from the RS485
}
void RS485_Analysis(uint8_t *buf)
{
  switch(buf[1])
  {
    case Extension_CH1:
      SetData(Send_Data[0],sizeof(Send_Data[0])); 
      printf("|***  Toggle expansion channel 1 ***|\r\n");
      break;
    case Extension_ALL_ON:
      SetData(Send_Data[8],sizeof(Send_Data[8])); 
      printf("|***  Enable all extension channels ***|\r\n");
      break;
    case Extension_ALL_OFF:
      SetData(Send_Data[9],sizeof(Send_Data[9])); 
      printf("|***  Close all expansion channels ***|\r\n");
      break;
    default:
      printf("Note : Non-control external device instructions !\r\n");
  }
}
void RS485_Init()                                             // Initializing serial port
{    
  constexpr uint32_t baudRate = 9600;
  lidarSerial.begin(baudRate, SERIAL_8N1, RXD1, TXD1);        // Initializing serial port
  if (!lidarSerial.setPins(-1, -1, -1, TXD1EN)) {
    printf("Failed to set TXDEN pins\r\n");
  }
  if (!lidarSerial.setMode(UART_MODE_RS485_HALF_DUPLEX)) {
    printf("Failed to set RS485 mode\r\n");
  }
  xTaskCreatePinnedToCore(
    RS485Task,    
    "RS485Task",   
    4096,                
    NULL,                 
    3,                   
    NULL,                 
    0                   
  );
}

void RS485Task(void *parameter) {
  while(1){
    RS485_Loop();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  vTaskDelete(NULL);
}

void RS485_Loop()
{
  static uint8_t frame[sizeof(relayCommands[0].frame)];
  static size_t length = 0;
  static uint32_t lastRead = 0;
  const uint32_t now = millis();
  if (now - lastRead > 50) {
    length = 0;
  }

  // Match complete eight-byte commands across UART reads and recover after noise.
  for (int remaining = lidarSerial.available(); remaining > 0; --remaining) {
    const int value = lidarSerial.read();
    if (value < 0) {
      return;
    }
    lastRead = now;
    frame[length++] = static_cast<uint8_t>(value);
    if (length < sizeof(frame)) {
      continue;
    }

    for (const auto &command : relayCommands) {
      if (memcmp(frame, command.frame, sizeof(frame)) == 0) {
        uint8_t relayCommand = command.action;
        Relay_Analysis(&relayCommand, RS485_Mode);
        length = 0;
        break;
      }
    }
    if (length != 0) {
      memmove(frame, frame + 1, sizeof(frame) - 1);
      length = sizeof(frame) - 1;
    }
  }
}
