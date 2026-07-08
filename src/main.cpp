#include <Arduino.h>
#include <EEPROM.h>
#include <GxEPD2_BW.h>
#include <FreeMonoBold18pt8b.h>
#include <FreeMonoBold9pt8b.h>
#include <WiFi.h>

#include "../config.h"

#include "apiHelper.h"
#include "buttonHelper.h"
#include "mealIcon.h"
#include "rotateIcon.h"
#include "rotateSlashIcon.h"
#include "penguinImage.h"

GxEPD2_BW<GxEPD2_270, GxEPD2_270::HEIGHT>
    display(GxEPD2_270(/*CS=5*/ SS, /*DC=*/17, /*RST=*/16,
                       /*BUSY=*/4)); // GDEW027W3 176x264, EK79652 (IL91874)

// list of names
char names[4][10] = {"0", "1", "2", "3"};

// list of counts
int counts[4] = {0};
int counts_today[4] = {0};

// number of times the sync failed
uint8_t syncRetryCount;

TaskHandle_t displayUpdateTaskHandle = nullptr;

#define FONT_9PT FreeMono_Bold9pt8b
#define FONT_18PT FreeMono_Bold18pt8b

void updateCountOnDisplay(int button)
{
  display.setPartialWindow(display.width() - 100, display.height() / 4 * button,
                           100, display.height() / 4 - display.height() / 8);
  display.firstPage();

  display.setFont(&FONT_9PT);
  display.setCursor(display.width() - 100,
                    display.height() / 4 * button + display.height() / 8);
  display.print(counts_today[button]);

  display.setFont(&FONT_18PT);
  display.setCursor(display.width() - 50,
                    display.height() / 4 * button + display.height() / 8);
  display.print(counts[button]);

  display.nextPage();
  display.hibernate();
}

String utf8ToLatin1(const String &in) {
  String out;
  out.reserve(in.length());

  const uint8_t *s = (const uint8_t *)in.c_str();

  while (*s) {
    uint8_t c = *s++;

    // Plain ASCII: unchanged
    if (c < 0x80) {
      out += (char)c;
    }

    // UTF-8 for Latin-1 range U+0080..U+00FF
    else if (c == 0xC2 || c == 0xC3) {
      uint8_t c2 = *s++;

      if ((c2 & 0xC0) == 0x80) {
        uint8_t latin1 = (c == 0xC2) ? c2 : (c2 + 0x40);
        out += (char)latin1;
      } else {
        out += '?';
      }
    }

    // Optional: map curly apostrophe ’ to normal apostrophe '
    else if (c == 0xE2 && s[0] == 0x80 && s[1] == 0x99) {
      s += 2;
      out += '\'';
    }

    // Anything not representable in Latin-1
    else {
      out += '?';

      // Skip UTF-8 continuation bytes, if any
      while ((*s & 0xC0) == 0x80) {
        s++;
      }
    }
  }

  return out;
}

void drawCounterScreen()
{
  // write four equally spaced lines on the screen and use relative height
  display.setFullWindow();
  display.firstPage();

  // print one name per line
  display.setFont(&FONT_18PT);
  for (int i = 0; i < 4; i++)
  {
    display.setCursor(0, display.height() / 4 * i + display.height() / 8);
    display.print(utf8ToLatin1(names[i]));
  }

  // print todays counts
  display.setFont(&FONT_9PT);
  for (int i = 0; i < 4; i++)
  {
    display.setFont(&FONT_9PT);
    display.setCursor(display.width() - 100,
                      display.height() / 4 * i + display.height() / 8);
    display.print(counts_today[i]);
  }

  // print the counts all aligned to the right
  display.setFont(&FONT_18PT);
  for (int i = 0; i < 4; i++)
  {
    display.setCursor(display.width() - 50,
                      display.height() / 4 * i + display.height() / 8);
    display.print(counts[i]);
  }

  display.nextPage();
}

void drawSleepScreen(String events[MAX_EVENT_COUNT], String mealPlannedToday,
                     uint8_t day, uint8_t month, String dayString)
{
  display.setFullWindow();
  display.firstPage();

  int16_t x, y;
  uint16_t w, h;

  display.setFont(&FONT_18PT);

  display.getTextBounds("01.01.", 0, 0, &x, &y, &w, &h);
  uint16_t baseY = 30 + h;
  display.setCursor((display.width() - w) / 2, baseY);
  display.printf("%02d.%02d.", day, month);

  display.setFont(&FONT_9PT);
  display.getTextBounds(dayString + ",", 0, 0, &x, &y, &w, &h);
  display.setCursor((display.width() - w) / 2, baseY - h - 15);
  display.print(dayString + ",");

  mealPlannedToday = utf8ToLatin1(mealPlannedToday);
  if (mealPlannedToday.length() > 22)
  {
    mealPlannedToday = mealPlannedToday.substring(0, 20);
    mealPlannedToday += "..";
  }

  if (events[0].length() == 0)
  {
    display.drawBitmap((display.width() - 120) / 2, baseY + 10, PENGUIN_IMAGE, 120, 120, GxEPD_BLACK);
  }

  baseY += h / 2 + 20;

  if (mealPlannedToday.length() > 0) {
    display.getTextBounds(mealPlannedToday, 0, 0, &x, &y, &w, &h);
    int todaysMealX = (display.width() - (w + 17 + 5)) / 2;

    display.drawBitmap(todaysMealX, baseY - (h / 2) - (17 / 2), MEAL_ICON, 17, 17,
                      GxEPD_BLACK);
    display.setCursor(todaysMealX + 17 + 5, baseY);

    display.print(mealPlannedToday);
  }

  baseY += h / 2 + 25;

  display.setFont(&FONT_9PT);
  for (int i = 0; i < MAX_EVENT_COUNT; i++)
  {
    if (events[i].length() == 0)
      continue;

    display.setCursor(0, baseY);
    display.print("| ");
    display.print(utf8ToLatin1(events[i]));
    baseY += 9 + 10;
  }

  display.nextPage();
}

void writeNamesToEeprom(String newNames[4])
{
  bool namesChanged = false;
  for (int i = 0; i < 4; i++)
  {
    if (strcmp(newNames[i].c_str(), names[i]) == 0)
      continue;

    namesChanged = true;
    newNames[i].toCharArray(names[i], 10);
  }

  if (namesChanged)
  {
    EEPROM.put(sizeof(counts), names);
    EEPROM.commit();
  }
}

void showSyncIcon(bool slash)
{
  display.setPartialWindow((display.width() - 50) / 2,
                           (display.height() - 50) / 2, 50, 50);
  display.firstPage();
  display.drawBitmap((display.width() - 50) / 2, (display.height() - 50) / 2,
                     slash ? ROTATE_SLASH_ICON : ROTATE_ICON, 50, 50,
                     GxEPD_BLACK);
  display.nextPage();
}

bool runSync(uint64_t &resyncInSeconds)
{
  resyncInSeconds = 60;

  // send to api
  bool ok = apiHelper::postCounts(counts);
  if (!ok)
  {
    return false;
  }

  // get motd
  String events[5],mealcountNames[4], mealPlannedToday, dayString;
  uint8_t day, month;

  ok = apiHelper::getMotd(events, mealcountNames, mealPlannedToday, resyncInSeconds, day, month, dayString);
  if (!ok)
  {
    return false;
  }
  resyncInSeconds += 60;

  writeNamesToEeprom(mealcountNames);
  drawSleepScreen(events, mealPlannedToday, day, month, dayString);

  return true;
}

void goSleep()
{
  if (displayUpdateTaskHandle != nullptr)
  {
    vTaskDelete(displayUpdateTaskHandle);
  }

  WiFi.begin(WIFI_SSID, WIFI_PSK);

  showSyncIcon(false);

  uint64_t resyncInSeconds;
  bool syncOk = runSync(resyncInSeconds);

  if (!syncOk)
  {
    syncRetryCount++;
    showSyncIcon(true);
  }
  else
  {
    syncRetryCount = 0;
  }

  if (syncRetryCount > 5)
  {
    resyncInSeconds = 60 * 60 * 24;
    syncRetryCount = 0;
  }

  Serial.println("Going to sleep for " + String(resyncInSeconds) + " seconds (tries: " + String(syncRetryCount) + ")");

  esp_sleep_enable_timer_wakeup(resyncInSeconds * 1000 * 1000);

  // write to eeprom
  EEPROM.put(0, counts);
  EEPROM.put(sizeof(counts) + sizeof(names), syncRetryCount);
  EEPROM.put(sizeof(counts) + sizeof(names) + sizeof(syncRetryCount), counts_today);
  EEPROM.commit();

  display.hibernate();
  esp_deep_sleep_start();
}

void handleButtonClick(int buttonIndex)
{
  counts_today[buttonIndex]++;
  counts[buttonIndex]++;
}

void handleButtonLongPress(int buttonIndex)
{
  counts_today[buttonIndex]--;
  counts[buttonIndex]--;
}

void handleButtonVeryLongPress(int buttonIndex) { counts[buttonIndex] = 0; }

void handleButtonOneAndFourClick(int)
{
  for (int i = 0; i < 4; i++)
  {
    counts[i] = 0;
  }
}

void updateDisplayTask(void *)
{
  int oldCounts[4] = {0};
  for (int i = 0; i < 4; i++)
  {
    oldCounts[i] = counts[i];
  }

  while (1)
  {
    for (int i = 0; i < 4; i++)
    {
      if (counts[i] != oldCounts[i])
      {
        oldCounts[i] = counts[i];
        updateCountOnDisplay(i);
      }
    }

    vTaskDelay(10);
  }
}

void setup()
{
  Serial.begin(115200);

  Serial.println("Starting up");

  // EEPROM layout:
  // 4*int (counts)
  // 4*char[10] (names)
  // 1*uint8_t (retry count)
  // 4*int (todays counts)

  // begin eeprom
  EEPROM.begin(sizeof(counts) + sizeof(names) + sizeof(syncRetryCount) + sizeof(counts_today));

  // read counts from eeprom
  EEPROM.get(0, counts);

  // read names from eeprom
  EEPROM.get(sizeof(counts), names);

  // read retry count from eeprom
  EEPROM.get(sizeof(counts) + sizeof(names), syncRetryCount);

  // read todays counts from eeprom
  EEPROM.get(sizeof(counts) + sizeof(names) + sizeof(syncRetryCount), counts_today);

  // dump eeprom as hex
  Serial.println("EEPROM:");
  for (int i = 0; i < sizeof(counts) + sizeof(names) + sizeof(syncRetryCount) + sizeof(counts_today); i++)
  {
    Serial.print(EEPROM.read(i), HEX);
    Serial.print(" ");
  }
  Serial.println();

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_12, 0);

  display.init(115200);
  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER)
  {
    Serial.println("Woke up from timer, sync retry count: " + String(syncRetryCount));

    // only reset if syncRetryCount is 0, because otherwise this may not be a midnight wakeup
    // but a retry wakeup, and we dont want to reset the counts in that case
    if (syncRetryCount == 0)
      for (int i = 0; i < 4; i++)
        counts_today[i] = 0;

    goSleep();
  }

  Serial.println("Woke up from button or reset");
  drawCounterScreen();
  xTaskCreate(&updateDisplayTask, "updateDisplay", 5000, NULL, 9,
              &displayUpdateTaskHandle);

  buttonHelper::init(handleButtonClick, handleButtonLongPress,
                     handleButtonVeryLongPress, handleButtonOneAndFourClick);
}

void loop()
{
  if (millis() - buttonHelper::getLastButtonActivity() > 20000)
  {
    goSleep();
  }

  vTaskDelay(100);
}
