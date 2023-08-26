#include <Arduino.h>
#include <EEPROM.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <GxEPD2_BW.h>
#include <WiFi.h>

#include "../config.h"

#include "apiHelper.h"
#include "buttonHelper.h"
#include "mealIcon.h"
#include "rotateIcon.h"
#include "rotateSlashIcon.h"

GxEPD2_BW<GxEPD2_270, GxEPD2_270::HEIGHT>
    display(GxEPD2_270(/*CS=5*/ SS, /*DC=*/17, /*RST=*/16,
                       /*BUSY=*/4)); // GDEW027W3 176x264, EK79652 (IL91874)

// list of names
char names[4][10] = {"0", "1", "2", "3"};

// list of counts
int counts[4] = {0};

TaskHandle_t displayUpdateTaskHandle = nullptr;

void updateCountOnDisplay(int button) {
  display.setFont(&FreeMonoBold18pt7b);
  display.setPartialWindow(display.width() - 50, display.height() / 4 * button,
                           50, display.height() / 4 - display.height() / 8);
  display.firstPage();
  display.setCursor(display.width() - 50,
                    display.height() / 4 * button + display.height() / 8);
  display.print(counts[button]);
  display.nextPage();
  display.hibernate();
}

void drawCounterScreen() {
  // write four equally spaced lines on the screen and use relative height
  display.setFont(&FreeMonoBold18pt7b);
  display.setFullWindow();
  display.firstPage();

  // print one name per line
  for (int i = 0; i < 4; i++) {
    display.setCursor(0, display.height() / 4 * i + display.height() / 8);
    display.print(names[i]);
  }

  // print the counts all aligned to the right
  for (int i = 0; i < 4; i++) {
    display.setCursor(display.width() - 50,
                      display.height() / 4 * i + display.height() / 8);
    display.print(counts[i]);
  }

  display.nextPage();
}

void drawSleepScreen(String events[MAX_EVENT_COUNT], String todaysMeal,
                     uint8_t day, uint8_t month, String dayString) {
  display.setFullWindow();
  display.firstPage();

  int16_t x, y;
  uint16_t w, h;

  display.setFont(&FreeMonoBold18pt7b);

  display.getTextBounds("01.01.", 0, 0, &x, &y, &w, &h);
  uint16_t baseY = 30 + h;
  display.setCursor((display.width() - w) / 2, baseY);
  display.printf("%02d.%02d.", day, month);

  display.setFont(&FreeMonoBold9pt7b);
  display.getTextBounds(dayString + ",", 0, 0, &x, &y, &w, &h);
  display.setCursor((display.width() - w) / 2, baseY - h - 15);
  display.print(dayString + ",");

  if (todaysMeal.length() > 17) {
    todaysMeal = todaysMeal.substring(0, 17);
    todaysMeal += "..";
  }

  baseY += h / 2 + 20;

  /*display.getTextBounds(todaysMeal, 0, 0, &x, &y, &w, &h);
  int todaysMealX = (display.width() - (w + 17 + 5)) / 2;

  display.drawBitmap(todaysMealX, baseY - (h / 2) - 4, MEAL_ICON, 17, 17,
                     GxEPD_BLACK);
  display.setCursor(todaysMealX + 17 + 5, baseY);

  display.print(todaysMeal);*/

  baseY += h / 2 + 25;

  display.setFont(&FreeMonoBold9pt7b);
  for (int i = 0; i < MAX_EVENT_COUNT; i++) {
    if (events[i].length() == 0)
      continue;

    display.setCursor(0, baseY);
    display.print("| ");
    display.print(events[i]);
    baseY += 9 + 10;
  }

  display.nextPage();
}

void writeNamesToEeprom(String newNames[4]) {
  bool namesChanged = false;
  for (int i = 0; i < 4; i++) {
    if (strcmp(newNames[i].c_str(), names[i]) == 0)
      continue;

    namesChanged = true;
    newNames[i].toCharArray(names[i], 10);
  }

  if (namesChanged) {
    EEPROM.put(sizeof(counts), names);
    EEPROM.commit();
  }
}

void showSyncIcon(bool slash) {
  display.setPartialWindow((display.width() - 50) / 2,
                           (display.height() - 50) / 2, 50, 50);
  display.firstPage();
  display.drawBitmap((display.width() - 50) / 2, (display.height() - 50) / 2,
                     slash ? ROTATE_SLASH_ICON : ROTATE_ICON, 50, 50,
                     GxEPD_BLACK);
  display.nextPage();
}

bool runSync(uint64_t &resyncInSeconds) {
  resyncInSeconds = 60;

  // send to api
  bool ok = apiHelper::postCounts(counts);
  if (!ok) {
    return false;
  }

  // get new names
  String newNames[4];
  ok = apiHelper::getNames(newNames);
  if (!ok) {
    return false;
  }
  writeNamesToEeprom(newNames);

  // get motd
  String events[5], dayString;
  uint8_t day, month;

  ok = apiHelper::getMotd(events, resyncInSeconds, day, month, dayString);
  if (!ok) {
    return false;
  }
  resyncInSeconds += 60;

  drawSleepScreen(events, "", day, month, dayString);

  return true;
}

void goSleep() {
  if (displayUpdateTaskHandle != nullptr) {
    vTaskDelete(displayUpdateTaskHandle);
  }

  // write to eeprom
  EEPROM.put(0, counts);
  EEPROM.commit();

  WiFi.begin(WIFI_SSID, WIFI_PSK);

  showSyncIcon(false);

  uint64_t resyncInSeconds;
  bool syncOk = runSync(resyncInSeconds);

  Serial.println("Going to sleep for " + String(resyncInSeconds) + " seconds");

  esp_sleep_enable_timer_wakeup(resyncInSeconds * 1000 * 1000);

  if (!syncOk) {
    showSyncIcon(true);
  }

  display.hibernate();
  esp_deep_sleep_start();
}

void handleButtonClick(int buttonIndex) { counts[buttonIndex]++; }

void handleButtonLongPress(int buttonIndex) { counts[buttonIndex]--; }

void handleButtonVeryLongPress(int buttonIndex) { counts[buttonIndex] = 0; }

void handleButtonOneAndFourClick(int) {
  for (int i = 0; i < 4; i++) {
    counts[i] = 0;
  }
}

void updateDisplayTask(void *) {
  int oldCounts[4] = {0};
  while (1) {
    for (int i = 0; i < 4; i++) {
      if (counts[i] != oldCounts[i]) {
        oldCounts[i] = counts[i];
        updateCountOnDisplay(i);
      }
    }

    vTaskDelay(10);
  }
}

void setup() {
  Serial.begin(115200);

  Serial.println("Starting up");

  // EEPROM layout:
  // 4*int (counts)
  // 4*char[10] (names)

  // begin eeprom
  EEPROM.begin(sizeof(counts) + sizeof(names));

  // read counts from eeprom
  EEPROM.get(0, counts);

  // read names from eeprom
  EEPROM.get(sizeof(counts), names);

  buttonHelper::init(handleButtonClick, handleButtonLongPress,
                     handleButtonVeryLongPress, handleButtonOneAndFourClick);

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_12, 0);

  display.init(115200);
  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Woke up from timer");
    goSleep();
  }

  Serial.println("Woke up from button or reset");
  drawCounterScreen();
  xTaskCreate(&updateDisplayTask, "updateDisplay", 5000, NULL, 9,
              &displayUpdateTaskHandle);
}

void loop() {
  if (millis() - buttonHelper::getLastButtonActivity() > 20000) {
    goSleep();
  }

  vTaskDelay(100);
}
