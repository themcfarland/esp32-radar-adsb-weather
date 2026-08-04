#pragma once

#include <Arduino.h>

namespace DisplayStability {

// Reduces RGB/PSRAM bandwidth and schedules a DMA realignment at VSYNC.
bool configureSafePixelClock(uint32_t pixelClockHz);

// Recovers the RGB stream if DMA and the panel have lost the same frame origin.
bool restartAtVsync();

}  // namespace DisplayStability
