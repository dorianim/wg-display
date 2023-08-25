#pragma once

#include <Arduino.h>

namespace buttonHelper {

typedef void (*ButtonCallback)(int);
void init(ButtonCallback handleButtonClick,
          ButtonCallback handleButtonLongPress,
          ButtonCallback handleButtonVeryLongPress,
          ButtonCallback handleButtonOneAndFourClick);

unsigned long getLastButtonActivity();
} // namespace buttonHelper