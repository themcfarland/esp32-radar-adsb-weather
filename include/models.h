#pragma once
#include <Arduino.h>
#include <strings.h>
#include "config.h"

struct Aircraft {
  char hex[9] = {0};
  char flight[13] = {0};
  float lat = NAN;
  float lon = NAN;
  float trackDeg = 0.0f;
  float groundSpeedKt = 0.0f;
  int32_t altitudeFt = -1;
  float seenPositionSec = 9999.0f;
};

constexpr size_t AIRCRAFT_ALERT_SLOT_COUNT = 3;

struct AircraftAlertConfig {
  bool enabled = false;
  char targets[AIRCRAFT_ALERT_SLOT_COUNT][17] = {{0}};
};

inline int8_t aircraftAlertMatchIndex(const Aircraft& aircraft,
                                      const AircraftAlertConfig& alert) {
  if (!alert.enabled) return -1;
  for (size_t i = 0; i < AIRCRAFT_ALERT_SLOT_COUNT; ++i) {
    if (!alert.targets[i][0]) continue;
    if (strcasecmp(aircraft.hex, alert.targets[i]) == 0 ||
        strcasecmp(aircraft.flight, alert.targets[i]) == 0) {
      return static_cast<int8_t>(i);
    }
  }
  return -1;
}

inline bool aircraftMatchesAlert(const Aircraft& aircraft,
                                 const AircraftAlertConfig& alert) {
  return aircraftAlertMatchIndex(aircraft, alert) >= 0;
}

struct AircraftSnapshot {
  Aircraft items[Config::MAX_AIRCRAFT];
  size_t count = 0;
  uint32_t generated = 0;
  bool valid = false;
  char endpoint[48] = {0};
  char status[80] = "ADSB: waiting";
};

struct CurrentWeather {
  bool valid = false;
  float temperatureC = NAN;
  float humidityPct = NAN;
  float pressureHpa = NAN;
  float windKph = NAN;
  float gustKph = NAN;
  float windDirectionDeg = NAN;
  float rainRateMmH = NAN;
  uint32_t epoch = 0;
};

struct ForecastSlot {
  bool valid = false;
  char timeText[12] = "--";
  int iconCode = 44;
  int temperatureC = 0;
  int precipChancePct = 0;
};

struct WeatherSnapshot {
  CurrentWeather current;
  ForecastSlot slots[3];
  float stationLat = Config::FALLBACK_LAT;
  float stationLon = Config::FALLBACK_LON;
  bool forecastValid = false;
  uint8_t forecastSlotCount = 0;
  char forecastProduct[16] = "--";
  char status[96] = "Weather: waiting";
};



constexpr size_t PRESSURE_HISTORY_POINT_COUNT = 289;

struct PressurePoint {
  uint32_t timestampMs = 0;
  // Sea-level pressure is shown in the 24-hour graph.
  float pressureHpa = NAN;
  // Unreduced station pressure drives the three-hour trend so changing
  // outdoor temperature cannot create a false pressure tendency.
  float stationPressureHpa = NAN;
};

struct BarometerSnapshot {
  bool enabled = true;
  bool detected = false;
  bool valid = false;
  uint8_t i2cAddress = 0;
  float rawPressureHpa = NAN;
  float pressureHpa = NAN;
  float temperatureC = NAN;
  float reductionTemperatureC = 15.0f;
  float wuTemperatureAverageC = NAN;
  uint16_t wuTemperatureSampleCount = 0;
  float wuTemperatureSpanHours = 0.0f;
  uint32_t wuTemperatureLatestEpoch = 0;
  char reductionTemperatureSource[24] = "standard 15 C";
  float trendHpaPerHour = 0.0f;
  float delta3hHpa = NAN;
  float projectedPressureHpa[3] = {NAN, NAN, NAN};
  PressurePoint history[PRESSURE_HISTORY_POINT_COUNT];
  size_t historyCount = 0;
  uint32_t historyRevision = 0;
  uint32_t sampleCount = 0;
  char sensorName[20] = "nenalezen";
  bool zambrettiReady = false;
  bool zambrettiWindUsed = false;
  bool zambrettiSeasonApplied = false;
  float zambrettiAdjustedPressureHpa = NAN;
  float zambrettiWindDirectionDeg = NAN;
  char zambrettiCode[3] = "-";
  char zambrettiTrend[16] = "sbiram";
  char trendText[32] = "sbiram data";
  char forecastText[80] = "Zambretti zatim neni k dispozici";
  char status[96] = "Barometr: cekam";
};

struct RuntimeDiagnostics {
  uint32_t uptimeMs = 0;
  uint32_t lastAdsbUpdateMs = 0;
  uint32_t lastRadarUpdateMs = 0;
  uint32_t lastLightningUpdateMs = 0;
  uint32_t lastCurrentWeatherUpdateMs = 0;
  uint32_t lastForecastUpdateMs = 0;
  uint32_t lastAstronomyUpdateMs = 0;
  uint32_t lastBarometerUpdateMs = 0;
  uint32_t lastDisplaySyncRecoveryMs = 0;
  uint32_t lcdResyncCount = 0;
  uint32_t mapRedrawCount = 0;
  uint32_t lastMapRedrawDurationMs = 0;
  uint8_t radarFrameCount = 0;
  uint8_t currentRadarFrame = 0;
  size_t lightningStrikeCount = 0;
  uint8_t forecastSlotCount = 0;
  size_t aircraftCount = 0;
  bool radarCacheReady = false;
  bool lightningReady = false;
  bool currentWeatherValid = false;
  float weatherPressureHpa = NAN;
  bool forecastValid = false;
  bool astronomyValid = false;
  bool barometerEnabled = true;
  bool barometerDetected = false;
  bool barometerValid = false;
  bool timeSynchronized = false;
  bool backlightOn = true;
  bool backlightScheduleEnabled = true;
  bool backlightScheduledWindowActive = true;
  bool backlightTemporaryWake = false;
  uint32_t backlightWakeRemainingMs = 0;
  char localTime[12] = "--:--:--";
  char localDate[16] = "--.--.----";
  char timezone[12] = "CET/CEST";
  char radarStatus[112] = "Radar: waiting";
  char lightningStatus[112] = "Blesky: waiting";
  char adsbStatus[80] = "ADSB: waiting";
  char weatherStatus[96] = "Weather: waiting";
  char astronomyStatus[48] = "Astronomy: waiting";
  char forecastProduct[16] = "--";
  char mapView[24] = "cela CR";
  uint8_t barometerAddress = 0;
  float barometerPressureHpa = NAN;
  float barometerRawPressureHpa = NAN;
  float barometerTemperatureC = NAN;
  float barometerReductionTemperatureC = 15.0f;
  float wuTemperatureAverageC = NAN;
  uint16_t wuTemperatureSampleCount = 0;
  float wuTemperatureSpanHours = 0.0f;
  uint32_t wuTemperatureLatestEpoch = 0;
  char barometerReductionTemperatureSource[24] = "standard 15 C";
  float barometerDelta3hHpa = NAN;
  float barometerTrendHpaPerHour = 0.0f;
  size_t pressureHistoryCount = 0;
  char barometerSensor[20] = "nenalezen";
  bool zambrettiReady = false;
  bool zambrettiWindUsed = false;
  bool zambrettiSeasonApplied = false;
  float zambrettiAdjustedPressureHpa = NAN;
  float zambrettiWindDirectionDeg = NAN;
  char zambrettiCode[3] = "-";
  char zambrettiTrend[16] = "sbiram";
  char barometerTrend[32] = "sbiram data";
  char barometerForecast[80] = "--";
  char barometerStatus[96] = "Barometr: cekam";
};

struct AstronomySnapshot {
  bool valid = false;
  char sunrise[6] = "--:--";
  char sunset[6] = "--:--";
  char moonrise[6] = "--:--";
  char moonset[6] = "--:--";
  float sunAltitudeDeg = NAN;
  float moonAltitudeDeg = NAN;
  float moonPhase = 0.0f;
  float moonIlluminationPct = 0.0f;
  char moonPhaseName[16] = "--";
  float latitude = Config::FALLBACK_LAT;
  float longitude = Config::FALLBACK_LON;
  uint32_t epoch = 0;
  char status[48] = "Astronomy: waiting";
};
