#include "apiHelper.h"

#include <ArduinoJson.h>
#include <WiFi.h>

#include "../config.h"

namespace apiHelper {

const char *apiUrl = API_URL;
const char *apiToken = API_TOKEN;

const char *apiNames[] = {"Adrian", "Jonas", "Dorian", "Tobias"};

bool isOnline() {
  for (int i = 0; i < 100 && !WiFi.isConnected(); i++) {
    delay(100);
  }

  if (!WiFi.isConnected())
    Serial.println("WiFi not connected");

  return WiFi.isConnected();
}

bool postCounts(int counts[4]) {
  if (!isOnline())
    return false;

  HTTPClient httpClient;

  String url = String(apiUrl) + "/mealcount";
  httpClient.setTimeout(30000);
  httpClient.begin(url);

  httpClient.addHeader("Content-Type", "application/json");
  httpClient.addHeader("Authorization", apiToken);

  String payload = "[";
  for (int i = 0; i < 4; i++) {
    payload += String(counts[i]) + ",";
  }

  payload.remove(payload.length() - 1, 1);
  payload += "]";

  int response = httpClient.POST(payload);

  Serial.println("POST " + url + " " + payload + " " + response);
  String responsePayload = httpClient.getString();
  Serial.println(responsePayload);

  httpClient.end();

  return response == 200;
}

bool getNames(String names[4]) {
  if (!isOnline())
    return false;

  HTTPClient httpClient;

  String url = String(apiUrl) + "/mealcount/names";
  httpClient.setTimeout(30000);
  httpClient.begin(url);

  httpClient.addHeader("Accept", "application/json");
  httpClient.addHeader("Authorization", apiToken);

  int response = httpClient.GET();

  Serial.println("GET " + url + " " + response);
  String responsePayload = httpClient.getString();
  Serial.println(responsePayload);

  httpClient.end();

  if (response != 200) {
    return false;
  }

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, responsePayload);

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return false;
  }

  for (int i = 0; i < 4; i++) {
    names[i] = doc[i].as<String>();
  }

  return true;
}

bool getMotd(String events[MAX_EVENT_COUNT], uint64_t &secondsUntilMidnight,
             uint8_t &day, uint8_t &month, String &dayString) {
  if (!isOnline())
    return false;

  HTTPClient httpClient;

  String url = String(apiUrl) + "/motd";
  httpClient.begin(url);

  httpClient.addHeader("Accept", "application/json");
  httpClient.addHeader("Authorization", apiToken);

  int response = httpClient.GET();
  Serial.println("GET " + url + " " + response);
  String responsePayload = httpClient.getString();
  Serial.println(responsePayload);

  httpClient.end();

  if (response != 200) {
    return false;
  }

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, responsePayload);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return false;
  }

  JsonArray eventsArray = doc["events"];
  for (int i = 0; i < eventsArray.size(); i++) {
    events[i] = eventsArray[i].as<String>();
  }

  secondsUntilMidnight = doc["secondsUntilMidnight"].as<uint64_t>();
  day = doc["day"].as<uint8_t>();
  month = doc["month"].as<uint8_t>();
  dayString = doc["dayString"].as<String>();

  return true;
}

} // namespace apiHelper