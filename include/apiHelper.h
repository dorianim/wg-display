#pragma once

#include "../config.h"
#include <Arduino.h>
#include <HTTPClient.h>

namespace apiHelper {
bool postCounts(int counts[4]);
bool getNames(String names[4]);
bool getMotd(String events[MAX_EVENT_COUNT], String mealcountNames[4], String &mealPlannedToday, uint64_t &secondsUntilMidnight,
             int64_t &timestamp, uint8_t &day, uint8_t &month, String &dayString);
} // namespace apiHelper