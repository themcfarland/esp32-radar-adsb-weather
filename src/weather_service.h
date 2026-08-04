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
    apiKey_ = apiKey;
    stationId_ = stationId;
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
