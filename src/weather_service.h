#pragma once

#include <Arduino.h>
#include "models.h"

class WeatherService {
 public:
  WeatherService(const char* apiKey, const char* stationId);

  bool update();
  bool updateCurrent();
  bool updateForecast();
  void setConfig(const String& apiKey, const String& stationId) {
    const bool stationChanged = stationId_ != stationId;
    apiKey_ = apiKey;
    stationId_ = stationId;
    if (stationChanged) {
      // Never expose the previous station's outdoor temperature to the
      // barometer after a runtime station change.
      snapshot_.current = CurrentWeather{};
    }
  }
  const WeatherSnapshot& snapshot() const { return snapshot_; }

 private:
  bool fetchCurrent();
  bool fetchForecast();
  bool fetchHourlyForecast(const char* duration, int& httpCode);
  bool fetchOpenMeteoForecast(int& httpCode);
  bool hasUsableWuKey() const;
  void clearForecast();

  String apiKey_;
  String stationId_;
  WeatherSnapshot snapshot_;
};
