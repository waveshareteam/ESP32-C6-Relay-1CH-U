#include <HardwareSerial.h>     // Reference the ESP32 built-in serial port library
#include "WS_Bluetooth.h"
#include "WS_GPIO.h"
#include "WS_RTC.h"
#include "WS_GPIO.h"
#include "WS_RS485.h"

uint32_t Simulated_time=0;      // Analog time counting

/********************************************************  Initializing  ********************************************************/
void setup() { 
  Serial.begin(115200);
  Relay_Init();
  I2C_Init();
  RTC_Init();   // RTC
  RS485_Init();
  WIFI_Init();
  Bluetooth_Init();
}

/**********************************************************  While  **********************************************************/
void loop() {

}