#pragma once
#include <Arduino.h>
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
  ForecastSlot slots[6];
  float stationLat = Config::FALLBACK_LAT;
  float stationLon = Config::FALLBACK_LON;
  bool forecastValid = false;
  uint8_t forecastSlotCount = 0;
  char forecastProduct[16] = "--";
  char status[96] = "Weather: waiting";
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
