#include "weather_service.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <math.h>
#include <time.h>

#include "config.h"
#include "debug_log.h"

namespace {
constexpr uint8_t kForecastHours[3] = {3, 6, 9};

void releaseSecureHttp(HTTPClient& http, WiFiClientSecure& client) {
  http.end();
  client.stop();
  delay(Config::TLS_POST_REQUEST_SETTLE_MS);
}

bool getHttpsJson(const String& url, DynamicJsonDocument& doc, int& httpCode) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.useHTTP10(true);  // Prevent HTTP/1.1 chunk framing in the JSON stream.
  http.setConnectTimeout(7000);
  http.setTimeout(18000);
  if (!http.begin(client, url)) {
    httpCode = -1;
    return false;
  }

  http.addHeader("Accept-Encoding", "identity");
  httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    releaseSecureHttp(http, client);
    return false;
  }

  const DeserializationError err = deserializeJson(doc, http.getStream());
  releaseSecureHttp(http, client);
  if (err) {
    httpCode = -2;
    return false;
  }
  return true;
}

size_t selectForecastIndex(JsonArrayConst validTimes, uint8_t hoursAhead,
                           uint32_t nowEpoch) {
  if (validTimes.isNull() || validTimes.size() == 0) return SIZE_MAX;

  if (nowEpoch > 1600000000UL) {
    const uint32_t target = nowEpoch + static_cast<uint32_t>(hoursAhead) * 3600UL;
    for (size_t i = 0; i < validTimes.size(); ++i) {
      if (validTimes[i].isNull()) continue;
      const uint32_t epoch = validTimes[i].as<uint32_t>();
      if (epoch >= target) return i;
    }

    // The requested product can end shortly before the exact target because
    // values are aligned to complete hours. Use the last valid value then.
    for (size_t i = validTimes.size(); i > 0; --i) {
      if (!validTimes[i - 1].isNull()) return i - 1;
    }
    return SIZE_MAX;
  }

  // Deterministic fallback until NTP is synchronized. Item 0 is normally the
  // next full forecast hour, hence +3 h is approximately item 2.
  const size_t fallback = hoursAhead > 0 ? hoursAhead - 1 : 0;
  return std::min(fallback, validTimes.size() - 1);
}

int arrayIntOr(JsonArrayConst array, size_t index, int fallback) {
  if (index >= array.size() || array[index].isNull()) return fallback;
  return static_cast<int>(lroundf(array[index].as<float>()));
}

bool formatLocalClock(uint32_t epoch, char* output, size_t outputSize) {
  if (!output || outputSize == 0 || epoch < 1600000000UL) return false;
  const time_t value = static_cast<time_t>(epoch);
  struct tm local {};
  localtime_r(&value, &local);
  return strftime(output, outputSize, "%H:%M", &local) > 0;
}

int wmoToTwcIcon(int code) {
  if (code == 0) return 32;
  if (code == 1) return 34;
  if (code == 2) return 30;
  if (code == 3) return 26;
  if (code == 45 || code == 48) return 26;
  if (code >= 51 && code <= 57) return 9;
  if (code >= 61 && code <= 67) return 11;
  if (code >= 71 && code <= 77) return 16;
  if (code >= 80 && code <= 82) return 40;
  if (code == 85 || code == 86) return 41;
  if (code == 95) return 4;
  if (code == 96 || code == 99) return 3;
  return 44;
}

String responsePreview(const String& payload, size_t maxLength = 120) {
  String preview;
  preview.reserve(std::min(maxLength, payload.length()));
  for (size_t i = 0; i < payload.length() && preview.length() < maxLength; ++i) {
    const char c = payload[i];
    preview += (c == '\r' || c == '\n' || c == '\t') ? ' ' : c;
  }
  return preview;
}
}  // namespace

WeatherService::WeatherService(const char* apiKey, const char* stationId)
    : apiKey_(apiKey ? apiKey : ""), stationId_(stationId ? stationId : "") {}

bool WeatherService::hasUsableWuKey() const {
  if (apiKey_.isEmpty()) return false;
  return apiKey_.indexOf("YOUR_") < 0 && apiKey_.indexOf("CHANGE_ME") < 0;
}


void WeatherService::applyCurrentSnapshot(const WeatherSnapshot& source) {
  snapshot_.stationLat = source.stationLat;
  snapshot_.stationLon = source.stationLon;
  snapshot_.current = source.current;
  if (source.status[0]) {
    strlcpy(snapshot_.status, source.status, sizeof(snapshot_.status));
  }
}

void WeatherService::applyForecastSnapshot(const WeatherSnapshot& source) {
  snapshot_.stationLat = source.stationLat;
  snapshot_.stationLon = source.stationLon;
  for (size_t i = 0; i < 3; ++i) snapshot_.slots[i] = source.slots[i];
  snapshot_.forecastValid = source.forecastValid;
  snapshot_.forecastSlotCount = source.forecastSlotCount;
  strlcpy(snapshot_.forecastProduct, source.forecastProduct,
          sizeof(snapshot_.forecastProduct));
  if (source.status[0]) {
    strlcpy(snapshot_.status, source.status, sizeof(snapshot_.status));
  }
}

bool WeatherService::update() {
  const bool currentOk = updateCurrent();
  const bool forecastOk = updateForecast();
  return currentOk || forecastOk;
}

bool WeatherService::updateCurrent() {
  if (WiFi.status() != WL_CONNECTED) {
    snapshot_.current.valid = false;
    strlcpy(snapshot_.status, "Weather: WiFi offline", sizeof(snapshot_.status));
    return false;
  }

  if (hasUsableWuKey() && !stationId_.isEmpty()) {
    DebugLog::println("PWS current: request started");
    if (fetchCurrent()) {
      snprintf(snapshot_.status, sizeof(snapshot_.status), "PWS OK | forecast %s",
               snapshot_.forecastValid ? snapshot_.forecastProduct : "--");
      DebugLog::printf("PWS current: OK, %.1f C\n", snapshot_.current.temperatureC);
      return true;
    }
    DebugLog::printf("PWS current: failed (%s), trying Open-Meteo current\n",
                     snapshot_.status);
  } else {
    DebugLog::println("Current weather: WU not configured, using Open-Meteo");
  }

  int openMeteoCode = 0;
  if (fetchOpenMeteoCurrent(openMeteoCode)) {
    snprintf(snapshot_.status, sizeof(snapshot_.status), "Open-Meteo current OK | forecast %s",
             snapshot_.forecastValid ? snapshot_.forecastProduct : "--");
    DebugLog::printf("Open-Meteo current: OK, %.1f C\n",
                     snapshot_.current.temperatureC);
    return true;
  }

  snapshot_.current.valid = false;
  snprintf(snapshot_.status, sizeof(snapshot_.status), "Current weather OM HTTP %d",
           openMeteoCode);
  return false;
}

bool WeatherService::updateForecast() {
  DebugLog::printf("Forecast: update started, WiFi=%s, free heap=%u kB\n",
                   WiFi.status() == WL_CONNECTED ? "connected" : "offline",
                   static_cast<unsigned>(ESP.getFreeHeap() / 1024));

  const bool ok = fetchForecast();
  if (ok) {
    snprintf(snapshot_.status, sizeof(snapshot_.status), "PWS %s | %s OK",
             snapshot_.current.valid ? "OK" : "--", snapshot_.forecastProduct);
    DebugLog::printf("Forecast: OK, source=%s, cards=%u\n",
                     snapshot_.forecastProduct,
                     static_cast<unsigned>(snapshot_.forecastSlotCount));
  } else {
    DebugLog::printf("Forecast: FAILED, status=%s\n", snapshot_.status);
  }
  DebugLog::flush();
  return ok;
}

bool WeatherService::fetchCurrent() {
  const String url =
      "https://api.weather.com/v2/pws/observations/current?stationId=" +
      stationId_ + "&format=json&units=m&numericPrecision=decimal&apiKey=" +
      apiKey_;

  DynamicJsonDocument doc(18 * 1024);
  int code = 0;
  if (!getHttpsJson(url, doc, code)) {
    snprintf(snapshot_.status, sizeof(snapshot_.status), "PWS HTTP %d", code);
    snapshot_.current.valid = false;
    return false;
  }

  JsonObjectConst obs = doc["observations"][0];
  if (obs.isNull()) {
    snprintf(snapshot_.status, sizeof(snapshot_.status), "PWS: no observation");
    snapshot_.current.valid = false;
    return false;
  }

  JsonObjectConst metric = obs["metric"];
  snapshot_.current.temperatureC = metric["temp"] | NAN;
  snapshot_.current.windKph = metric["windSpeed"] | NAN;
  snapshot_.current.gustKph = metric["windGust"] | NAN;
  snapshot_.current.windDirectionDeg = obs["winddir"] | NAN;
  snapshot_.current.pressureHpa = metric["pressure"] | NAN;
  snapshot_.current.rainRateMmH = metric["precipRate"] | NAN;
  snapshot_.current.humidityPct = obs["humidity"] | NAN;
  snapshot_.current.epoch = obs["epoch"] | 0;
  snapshot_.current.valid = true;

  return true;
}

bool WeatherService::fetchOpenMeteoCurrent(int& httpCode) {
  const String latitude = String(snapshot_.stationLat, 5);
  const String longitude = String(snapshot_.stationLon, 5);
  const String url =
      "https://api.open-meteo.com/v1/forecast?latitude=" + latitude +
      "&longitude=" + longitude +
      "&current=temperature_2m,relative_humidity_2m,precipitation,rain,pressure_msl,wind_speed_10m,wind_direction_10m,wind_gusts_10m"
      "&wind_speed_unit=kmh&timeformat=unixtime&timezone=GMT";

  DebugLog::printf("Current Open-Meteo: request started for %s,%s\n",
                   latitude.c_str(), longitude.c_str());
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setConnectTimeout(7000);
  http.setTimeout(18000);
  if (!http.begin(client, url)) {
    httpCode = -1;
    return false;
  }
  http.addHeader("Accept-Encoding", "identity");
  httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    releaseSecureHttp(http, client);
    return false;
  }

  const String payload = http.getString();
  releaseSecureHttp(http, client);
  if (payload.isEmpty()) {
    httpCode = -2;
    return false;
  }

  DynamicJsonDocument doc(6 * 1024);
  const DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    DebugLog::printf("Current Open-Meteo: JSON error %s\n", err.c_str());
    httpCode = -2;
    return false;
  }

  JsonObjectConst current = doc["current"];
  if (current.isNull()) {
    httpCode = -3;
    return false;
  }

  snapshot_.current.temperatureC = current["temperature_2m"] | NAN;
  snapshot_.current.humidityPct = current["relative_humidity_2m"] | NAN;
  snapshot_.current.pressureHpa = current["pressure_msl"] | NAN;
  snapshot_.current.windKph = current["wind_speed_10m"] | NAN;
  snapshot_.current.gustKph = current["wind_gusts_10m"] | NAN;
  snapshot_.current.windDirectionDeg = current["wind_direction_10m"] | NAN;
  float rain = current["rain"] | NAN;
  if (!isfinite(rain)) rain = current["precipitation"] | NAN;
  snapshot_.current.rainRateMmH = rain;
  snapshot_.current.epoch = current["time"] | static_cast<uint32_t>(time(nullptr));
  snapshot_.current.valid = isfinite(snapshot_.current.temperatureC);
  return snapshot_.current.valid;
}

void WeatherService::clearForecast() {
  for (ForecastSlot& slot : snapshot_.slots) slot = ForecastSlot{};
  snapshot_.forecastValid = false;
  snapshot_.forecastSlotCount = 0;
  strlcpy(snapshot_.forecastProduct, "--", sizeof(snapshot_.forecastProduct));
}

bool WeatherService::fetchHourlyForecast(const char* duration, int& httpCode) {
  const String latitude = String(snapshot_.stationLat, 4);
  const String longitude = String(snapshot_.stationLon, 4);
  const String url =
      "https://api.weather.com/v3/wx/forecast/hourly/" + String(duration) +
      "?geocode=" + latitude + "," + longitude +
      "&format=json&units=m&language=cs-CZ&apiKey=" + apiKey_;

  DebugLog::printf("Forecast WU %s: request started for %s,%s\n", duration,
                   latitude.c_str(), longitude.c_str());

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setConnectTimeout(7000);
  http.setTimeout(18000);
  if (!http.begin(client, url)) {
    httpCode = -1;
    DebugLog::printf("Forecast WU %s: http.begin failed\n", duration);
    return false;
  }

  http.addHeader("Accept-Encoding", "identity");
  httpCode = http.GET();
  DebugLog::printf("Forecast WU %s: HTTP %d\n", duration, httpCode);
  if (httpCode != HTTP_CODE_OK) {
    releaseSecureHttp(http, client);
    return false;
  }

  StaticJsonDocument<512> filter;
  filter["validTimeUtc"][0] = true;
  filter["iconCode"][0] = true;
  filter["temperature"][0] = true;
  filter["precipChance"][0] = true;

  DynamicJsonDocument doc(14 * 1024);
  const DeserializationError err = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  releaseSecureHttp(http, client);
  if (err) {
    DebugLog::printf("Forecast WU %s: JSON error %s\n", duration, err.c_str());
    httpCode = -2;
    return false;
  }

  JsonArrayConst times = doc["validTimeUtc"].as<JsonArrayConst>();
  JsonArrayConst icons = doc["iconCode"].as<JsonArrayConst>();
  JsonArrayConst temperatures = doc["temperature"].as<JsonArrayConst>();
  JsonArrayConst precipitation = doc["precipChance"].as<JsonArrayConst>();

  const size_t available = std::min(
      std::min(times.size(), icons.size()),
      std::min(temperatures.size(), precipitation.size()));
  DebugLog::printf("Forecast WU %s: parsed %u hourly records\n", duration,
                   static_cast<unsigned>(available));
  if (available < 10) {
    httpCode = -3;
    return false;
  }

  clearForecast();
  const time_t nowTime = time(nullptr);
  const uint32_t nowEpoch = nowTime > 1600000000L
                                ? static_cast<uint32_t>(nowTime)
                                : 0;
  uint8_t validSlots = 0;

  for (size_t slotIndex = 0; slotIndex < 3; ++slotIndex) {
    const uint8_t hoursAhead = kForecastHours[slotIndex];
    const size_t sourceIndex = selectForecastIndex(times, hoursAhead, nowEpoch);
    if (sourceIndex == SIZE_MAX || sourceIndex >= available) continue;

    ForecastSlot& target = snapshot_.slots[slotIndex];
    target.valid = true;
    const uint32_t slotEpoch = times[sourceIndex].as<uint32_t>();
    if (!formatLocalClock(slotEpoch, target.timeText, sizeof(target.timeText))) {
      snprintf(target.timeText, sizeof(target.timeText), "+%u h",
               static_cast<unsigned>(hoursAhead));
    }
    target.temperatureC = arrayIntOr(temperatures, sourceIndex, 0);
    target.iconCode = arrayIntOr(icons, sourceIndex, 44);
    target.precipChancePct = arrayIntOr(precipitation, sourceIndex, 0);
    ++validSlots;
  }

  if (validSlots != 3) {
    DebugLog::printf("Forecast WU %s: only %u/3 cards selected\n", duration,
                     static_cast<unsigned>(validSlots));
    httpCode = -4;
    clearForecast();
    return false;
  }

  snapshot_.forecastValid = true;
  snapshot_.forecastSlotCount = validSlots;
  snprintf(snapshot_.forecastProduct, sizeof(snapshot_.forecastProduct),
           "WU %s", duration);
  return true;
}

bool WeatherService::fetchOpenMeteoForecast(int& httpCode) {
  const String latitude = String(snapshot_.stationLat, 4);
  const String longitude = String(snapshot_.stationLon, 4);
  const String url =
      "https://api.open-meteo.com/v1/forecast?latitude=" + latitude +
      "&longitude=" + longitude +
      "&hourly=temperature_2m,precipitation_probability,weather_code"
      "&forecast_hours=12&timeformat=unixtime&timezone=GMT";

  DebugLog::printf("Forecast Open-Meteo: request started for %s,%s\n",
                   latitude.c_str(), longitude.c_str());

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  // ArduinoJson cannot parse raw HTTP/1.1 chunk markers from getStream().
  // HTTP/1.0 plus identity encoding requests a plain response body.
  http.useHTTP10(true);
  http.setConnectTimeout(7000);
  http.setTimeout(18000);
  if (!http.begin(client, url)) {
    httpCode = -1;
    DebugLog::println("Forecast Open-Meteo: http.begin failed");
    return false;
  }

  http.addHeader("Accept-Encoding", "identity");
  httpCode = http.GET();
  DebugLog::printf("Forecast Open-Meteo: HTTP %d, declared length=%d\n",
                   httpCode, http.getSize());
  if (httpCode != HTTP_CODE_OK) {
    const String errorBody = http.getString();
    if (!errorBody.isEmpty()) {
      DebugLog::printf("Forecast Open-Meteo: error body: %s\n",
                       responsePreview(errorBody).c_str());
    }
    releaseSecureHttp(http, client);
    return false;
  }

  // getString() lets HTTPClient remove transport framing before ArduinoJson
  // sees the data. The 12-hour response is only a few kilobytes and the
  // forecast is downloaded once per hour.
  String payload = http.getString();
  releaseSecureHttp(http, client);
  DebugLog::printf("Forecast Open-Meteo: body received, %u bytes\n",
                   static_cast<unsigned>(payload.length()));
  if (payload.isEmpty()) {
    DebugLog::println("Forecast Open-Meteo: empty response body");
    httpCode = -2;
    return false;
  }

  size_t firstNonSpace = 0;
  while (firstNonSpace < payload.length() &&
         (payload[firstNonSpace] == ' ' || payload[firstNonSpace] == '\r' ||
          payload[firstNonSpace] == '\n' || payload[firstNonSpace] == '\t')) {
    ++firstNonSpace;
  }
  if (firstNonSpace >= payload.length() || payload[firstNonSpace] != '{') {
    DebugLog::printf("Forecast Open-Meteo: unexpected body: %s\n",
                     responsePreview(payload).c_str());
    httpCode = -2;
    return false;
  }

  DynamicJsonDocument doc(14 * 1024);
  const DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    DebugLog::printf("Forecast Open-Meteo: JSON error %s, body: %s\n",
                     err.c_str(), responsePreview(payload).c_str());
    httpCode = -2;
    return false;
  }

  JsonObjectConst hourly = doc["hourly"];
  JsonArrayConst times = hourly["time"].as<JsonArrayConst>();
  JsonArrayConst temperatures = hourly["temperature_2m"].as<JsonArrayConst>();
  JsonArrayConst precipitation =
      hourly["precipitation_probability"].as<JsonArrayConst>();
  JsonArrayConst weatherCodes = hourly["weather_code"].as<JsonArrayConst>();

  const size_t available = std::min(
      std::min(times.size(), temperatures.size()),
      std::min(precipitation.size(), weatherCodes.size()));
  DebugLog::printf("Forecast Open-Meteo: parsed %u hourly records\n",
                   static_cast<unsigned>(available));
  if (available < 10) {
    httpCode = -3;
    return false;
  }

  clearForecast();
  const time_t nowTime = time(nullptr);
  const uint32_t nowEpoch = nowTime > 1600000000L
                                ? static_cast<uint32_t>(nowTime)
                                : 0;
  uint8_t validSlots = 0;

  for (size_t slotIndex = 0; slotIndex < 3; ++slotIndex) {
    const uint8_t hoursAhead = kForecastHours[slotIndex];
    const size_t sourceIndex = selectForecastIndex(times, hoursAhead, nowEpoch);
    if (sourceIndex == SIZE_MAX || sourceIndex >= available) continue;

    ForecastSlot& target = snapshot_.slots[slotIndex];
    target.valid = true;
    const uint32_t slotEpoch = times[sourceIndex].as<uint32_t>();
    if (!formatLocalClock(slotEpoch, target.timeText, sizeof(target.timeText))) {
      snprintf(target.timeText, sizeof(target.timeText), "+%u h",
               static_cast<unsigned>(hoursAhead));
    }
    target.temperatureC = arrayIntOr(temperatures, sourceIndex, 0);
    target.precipChancePct = arrayIntOr(precipitation, sourceIndex, 0);
    target.iconCode = wmoToTwcIcon(arrayIntOr(weatherCodes, sourceIndex, 3));
    ++validSlots;
  }

  if (validSlots != 3) {
    DebugLog::printf("Forecast Open-Meteo: only %u/3 cards selected\n",
                     static_cast<unsigned>(validSlots));
    httpCode = -4;
    clearForecast();
    return false;
  }

  snapshot_.forecastValid = true;
  snapshot_.forecastSlotCount = validSlots;
  strlcpy(snapshot_.forecastProduct, "Open-Meteo",
          sizeof(snapshot_.forecastProduct));
  return true;
}

bool WeatherService::fetchForecast() {
  clearForecast();

  if (WiFi.status() != WL_CONNECTED) {
    strlcpy(snapshot_.status, "Forecast: WiFi offline", sizeof(snapshot_.status));
    return false;
  }

  // Open-Meteo is the primary forecast source because a PWS API key often
  // authorizes observations but not the separate TWC hourly product.
  int openMeteoCode = 0;
  if (fetchOpenMeteoForecast(openMeteoCode)) return true;

  int code2 = 0;
  int code3 = 0;
  if (hasUsableWuKey()) {
    if (fetchHourlyForecast("2day", code2)) return true;
    if (fetchHourlyForecast("3day", code3)) return true;
  } else {
    code2 = code3 = -10;
    DebugLog::println("Forecast WU: skipped - API key is not configured");
  }

  snprintf(snapshot_.status, sizeof(snapshot_.status),
           "Forecast OM:%d WU2:%d WU3:%d", openMeteoCode, code2, code3);
  return false;
}
