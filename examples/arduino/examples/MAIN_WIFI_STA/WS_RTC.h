#pragma once

#include "WS_PCF85063.h"
#include "WS_Relay.h"

typedef enum : uint8_t {
  Repetition_NONE = 0,
  Repetition_everyday = 1,
  Repetition_Weekly = 2,
  Repetition_monthly = 3,
} Repetition_event;

void RTC_Init();
bool TimerEvent_Add(datetime_t time, Status_adjustment state, Repetition_event repetition);
bool TimerEvent_Del_Number(long eventNumber);
String TimerEvent_Json();
