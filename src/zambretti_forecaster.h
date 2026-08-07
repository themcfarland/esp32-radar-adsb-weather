#pragma once

#include <stdint.h>

namespace Zambretti {

enum class Trend : uint8_t {
  Steady = 0,
  Rising,
  Falling,
};

struct Result {
  bool valid = false;
  char code = '-';
  Trend trend = Trend::Steady;
  float adjustedPressureHpa = 0.0f;
  bool windUsed = false;
  bool seasonalCorrectionApplied = false;
};

// Classic Northern-Hemisphere Zambretti calculation. Pressure must already be
// reduced to mean sea level. trendHpaPerHour is derived from recent pressure
// history. Wind direction is optional and expressed in degrees, where 0=N.
Result forecast(float seaLevelPressureHpa, float trendHpaPerHour,
                uint8_t month, bool monthValid,
                float windDirectionDeg, bool windValid,
                bool northernHemisphere = true);

const char* forecastTextCs(char code);
const char* trendTextCs(Trend trend);

}  // namespace Zambretti
