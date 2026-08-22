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
  void setLocation(float latitude, float longitude) {
    if (!isfinite(latitude) || !isfinite(longitude)) return;
    const bool changed = fabsf(snapshot_.stationLat - latitude) > 0.00001f ||
                         fabsf(snapshot_.stationLon - longitude) > 0.00001f;
    snapshot_.stationLat = latitude;
    snapshot_.stationLon = longitude;
    if (changed) {
      snapshot_.current = CurrentWeather{};
      for (ForecastSlot& slot : snapshot_.slots) slot = ForecastSlot{};
      snapshot_.forecastValid = false;
      snapshot_.forecastSlotCount = 0;
      strlcpy(snapshot_.forecastProduct, "--", sizeof(snapshot_.forecastProduct));
    }
  }
  const WeatherSnapshot& snapshot() const { return snapshot_; }
  // Apply a completed background fetch in the main/UI task. Failed worker
  // requests never call these methods, so the last good weather data remain
  // visible during outages.
  void applyCurrentSnapshot(const WeatherSnapshot& source);
  void applyForecastSnapshot(const WeatherSnapshot& source);

 private:
  bool fetchCurrent();
  bool fetchOpenMeteoCurrent(int& httpCode);
  bool fetchForecast();
  bool fetchHourlyForecast(const char* duration, int& httpCode);
  bool fetchOpenMeteoForecast(int& httpCode);
  bool hasUsableWuKey() const;
  void clearForecast();

  String apiKey_;
  String stationId_;
  WeatherSnapshot snapshot_;
};
