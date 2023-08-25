#include "buttonHelper.h"

namespace buttonHelper {
enum ButtonEvent {
  PRESSED,
  LONG_PRESSED,
  VERY_LONG_PRESSED,
  COMBINATION_PRESSED,
  RELEASED
};

struct Button {
  int pin;
  unsigned long pressedAt;
  unsigned long lastEventAt;
  ButtonEvent lastEvent;
};

ButtonCallback handleButtonClick;
ButtonCallback handleButtonLongPress;
ButtonCallback handleButtonVeryLongPress;
ButtonCallback handleButtonOneAndFourClick;

Button buttons[] = {{12, 0, 0, RELEASED},
                    {14, 0, 0, RELEASED},
                    {27, 0, 0, RELEASED},
                    {26, 0, 0, RELEASED}};
unsigned long lastButtonActivity = 0;

bool checkButtonPressed(int buttonIndex) {
  if (digitalRead(buttons[buttonIndex].pin) == HIGH ||
      buttons[buttonIndex].pressedAt != 0)
    return false;

  buttons[buttonIndex].pressedAt = millis();
  buttons[buttonIndex].lastEventAt = millis();
  buttons[buttonIndex].lastEvent = PRESSED;
  return true;
}

bool checkButtonReleased(int buttonIndex) {
  if (digitalRead(buttons[buttonIndex].pin) == LOW ||
      buttons[buttonIndex].pressedAt == 0)
    return false;

  buttons[buttonIndex].pressedAt = 0;
  buttons[buttonIndex].lastEventAt = millis();

  if (buttons[buttonIndex].lastEvent == PRESSED)
    handleButtonClick(buttonIndex);

  buttons[buttonIndex].lastEvent = RELEASED;
  return true;
}

bool checkButtonLongPressed(int buttonIndex) {
  if (buttons[buttonIndex].pressedAt == 0)
    return false;

  unsigned long pressDuration = millis() - buttons[buttonIndex].pressedAt;

  if (pressDuration >= 2000 && buttons[buttonIndex].lastEvent < LONG_PRESSED) {
    buttons[buttonIndex].lastEvent = LONG_PRESSED;
    handleButtonLongPress(buttonIndex);
  } else if (pressDuration >= 4000 &&
             buttons[buttonIndex].lastEvent < VERY_LONG_PRESSED) {
    buttons[buttonIndex].lastEvent = VERY_LONG_PRESSED;
    handleButtonVeryLongPress(buttonIndex);
  } else {
    return false;
  }

  buttons[buttonIndex].lastEventAt = millis();
  return true;
}

bool checkButtonOneAndFourPressed() {
  if (buttons[0].lastEvent != PRESSED || buttons[3].lastEvent != PRESSED)
    return false;

  handleButtonOneAndFourClick(0);
  buttons[0].lastEvent = COMBINATION_PRESSED;
  buttons[3].lastEvent = COMBINATION_PRESSED;
  return true;
}

void watchButtons(void *) {
  while (1) {
    bool activityDetected = false;
    for (int i = 0; i < 4; i++) {
      activityDetected = checkButtonPressed(i) || activityDetected;
      activityDetected = checkButtonLongPressed(i) || activityDetected;
      activityDetected = checkButtonReleased(i) || activityDetected;
    }

    activityDetected = checkButtonOneAndFourPressed() || activityDetected;

    if (activityDetected)
      lastButtonActivity = millis();

    vTaskDelay(10);
  }
}

void init(ButtonCallback handleButtonClick,
          ButtonCallback handleButtonLongPress,
          ButtonCallback handleButtonVeryLongPress,
          ButtonCallback handleButtonOneAndFourClick) {
  buttonHelper::handleButtonClick = handleButtonClick;
  buttonHelper::handleButtonLongPress = handleButtonLongPress;
  buttonHelper::handleButtonVeryLongPress = handleButtonVeryLongPress;
  buttonHelper::handleButtonOneAndFourClick = handleButtonOneAndFourClick;

  for (int i = 0; i < 4; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
  }

  for (int i = 0; i < 4; i++) {
    while (digitalRead(buttons[i].pin) == LOW)
      ;
  }

  xTaskCreate(&watchButtons, "watchButtons", 5000, NULL, 10, NULL);
}

unsigned long getLastButtonActivity() { return lastButtonActivity; }

} // namespace buttonHelper