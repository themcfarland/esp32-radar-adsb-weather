#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "models.h"

namespace UI {

bool begin();
lv_obj_t* mapCanvas();
uint16_t* mapBuffer();
void presentMap();
void invalidateMap();  // Backward-compatible alias for presentMap().
void updateWeather(const WeatherSnapshot& weather);
void updateAstronomy(const AstronomySnapshot& astronomy);
void updateHeader(const char* networkStatus, const char* radarStatus,
                  const AircraftSnapshot& aircraft);
bool radarPaused();
bool consumeManualRefresh();
bool consumeMapTap(int16_t& x, int16_t& y);
void setBacklightWakeOverlay(bool enabled);
bool consumeBacklightWakeRequest();

}  // namespace UI
