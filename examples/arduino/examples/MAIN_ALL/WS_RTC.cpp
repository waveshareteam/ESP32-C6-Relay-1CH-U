#include "WS_RTC.h"

#include <mutex>

struct TimingEvent {
  datetime_t time;
  Status_adjustment state;
  Repetition_event repetition;
};

static constexpr size_t maxEvents = 10;
static TimingEvent events[maxEvents];
static size_t eventCount = 0;
static std::mutex eventMutex;

static bool validDateTime(const datetime_t &time) {
  if (time.year == 0 || time.month < 1 || time.month > 12 || time.day < 1 ||
      time.dotw > 6 || time.hour > 23 || time.minute > 59 || time.second > 59) {
    return false;
  }

  constexpr uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leapYear = time.year % 4 == 0 && (time.year % 100 != 0 || time.year % 400 == 0);
  return time.day <= daysInMonth[time.month - 1] + (time.month == 2 && leapYear);
}

// Caller holds eventMutex.
static void removeEvent(size_t index) {
  for (size_t i = index + 1; i < eventCount; ++i) {
    events[i - 1] = events[i];
  }
  --eventCount;
}

bool TimerEvent_Add(datetime_t time, Status_adjustment state, Repetition_event repetition) {
  if (!validDateTime(time) || state < STATE_Closs || state > STATE_Retain ||
      repetition > Repetition_monthly) {
    return false;
  }

  std::lock_guard<std::mutex> lock(eventMutex);
  if (eventCount == maxEvents) {
    return false;
  }
  events[eventCount++] = {time, state, repetition};
  return true;
}

bool TimerEvent_Del_Number(long eventNumber) {
  std::lock_guard<std::mutex> lock(eventMutex);
  if (eventNumber < 1 || eventNumber > static_cast<long>(eventCount)) {
    return false;
  }
  removeEvent(static_cast<size_t>(eventNumber - 1));
  return true;
}

String TimerEvent_Json() {
  const datetime_t now = PCF85063_Get_Time();
  char currentTime[64] = "RTC time unavailable";
  if (validDateTime(now)) {
    snprintf(currentTime, sizeof(currentTime), "%04u/%02u/%02u %s %02u:%02u:%02u",
             now.year, now.month, now.day, Week[now.dotw], now.hour, now.minute, now.second);
  }
  String response = "{\"time\":\"" + String(currentTime) + "\",";

  TimingEvent snapshot[maxEvents];
  size_t count;
  {
    std::lock_guard<std::mutex> lock(eventMutex);
    count = eventCount;
    for (size_t i = 0; i < count; ++i) {
      snapshot[i] = events[i];
    }
  }

  const char *const states[] = {"Close", "Open", "Retain"};
  const char *const cycles[] = {"Once", "Daily", "Weekly", "Monthly"};
  for (size_t i = 0; i < count; ++i) {
    const TimingEvent &event = snapshot[i];
    const datetime_t &time = event.time;
    char item[160];
    const unsigned int number = i + 1;
    snprintf(item, sizeof(item),
             "\"eventStr%u\":\"Event %u: %04u/%02u/%02u %s %02u:%02u:%02u\\nCH1: %s (%s)\",",
             number, number, time.year, time.month, time.day, Week[time.dotw],
             time.hour, time.minute, time.second, states[event.state], cycles[event.repetition]);
    response += item;
  }
  response += "\"eventCount\":" + String(count) + "}";
  return response;
}

static bool eventIsDue(const TimingEvent &event, const datetime_t &now) {
  const datetime_t &time = event.time;
  if (time.hour != now.hour || time.minute != now.minute || time.second != now.second) {
    return false;
  }

  switch (event.repetition) {
    case Repetition_NONE:
      return time.year == now.year && time.month == now.month && time.day == now.day;
    case Repetition_everyday:
      return true;
    case Repetition_Weekly:
      return time.dotw == now.dotw;
    case Repetition_monthly:
      return time.day == now.day;
  }
  return false;
}

static void RTCTask(void *) {
  uint8_t lastSecond = UINT8_MAX;
  while (true) {
    const datetime_t now = PCF85063_Get_Time();
    if (now.second != lastSecond && validDateTime(now)) {
      lastSecond = now.second;
      std::lock_guard<std::mutex> lock(eventMutex);
      for (size_t i = 0; i < eventCount;) {
        const TimingEvent &event = events[i];
        if (eventIsDue(event, now)) {
          if (event.state != STATE_Retain) {
            Relay_Immediate(0, event.state == STATE_Open, RTC_Mode);
          }
          if (event.repetition == Repetition_NONE) {
            removeEvent(i);
            continue;
          }
        }
        ++i;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void RTC_Init() {
  PCF85063_Init();
  xTaskCreatePinnedToCore(RTCTask, "RTCTask", 4096, nullptr, 3, nullptr, 0);
}
