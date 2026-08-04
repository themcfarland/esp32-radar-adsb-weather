#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "map_viewport.h"
#include "models.h"

namespace MapRenderer {

MapViewport makeViewport(MapZoomMode mode, float centerLat, float centerLon,
                         uint16_t width, uint16_t height);
MapZoomMode nextZoomMode(MapZoomMode mode);
bool screenToGeo(const MapViewport& viewport, int x, int y, uint16_t width,
                 uint16_t height, float& lat, float& lon);
const char* zoomModeLabel(MapZoomMode mode);

void drawBase(lv_obj_t* canvas, uint16_t* buffer, uint16_t width,
              uint16_t height, const MapViewport& viewport);
void drawReference(lv_obj_t* canvas, uint16_t* buffer, uint16_t width,
                   uint16_t height, const MapViewport& viewport);
void drawAircraft(lv_obj_t* canvas, uint16_t* buffer, uint16_t width,
                  uint16_t height, const AircraftSnapshot& aircraft,
                  const MapViewport& viewport);
void drawRadarAge(lv_obj_t* canvas, uint16_t* buffer, uint16_t width,
                  uint16_t height, const char* frameName, uint8_t frameIndex,
                  uint8_t frameCount, uint16_t sourceWidth,
                  uint16_t sourceHeight);
void drawRadarMessage(lv_obj_t* canvas, uint16_t* buffer, uint16_t width,
                      uint16_t height, const char* message);

}  // namespace MapRenderer
