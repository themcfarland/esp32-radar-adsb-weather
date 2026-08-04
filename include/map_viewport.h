#pragma once

#include <stdint.h>

enum class MapZoomMode : uint8_t {
  Full = 0,
  Km50 = 1,
  Km25 = 2,
  Km10 = 3,
};

struct MapViewport {
  MapZoomMode mode = MapZoomMode::Full;
  float centerLat = 49.8f;
  float centerLon = 15.35f;
  float lonLeft = 11.7f;
  float lonRight = 19.0f;
  float latTop = 51.3f;
  float latBottom = 48.3f;
  uint16_t radiusKm = 0;
};
