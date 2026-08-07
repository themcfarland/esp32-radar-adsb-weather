#include "zambretti_forecaster.h"

#include <math.h>
#include <stddef.h>

namespace Zambretti {
namespace {
constexpr float kPressureBottomHpa = 950.0f;
constexpr float kPressureTopHpa = 1050.0f;
constexpr float kTrendThresholdHpaPerHour = 0.10f;

constexpr float kWindPressureAdjustmentHpa[16] = {
    5.20f, 4.20f, 3.20f, 1.05f,
   -1.10f,-3.15f,-5.20f,-8.35f,
  -11.50f,-9.40f,-7.30f,-5.25f,
   -3.20f,-1.15f, 0.90f, 3.05f,
};

constexpr char kRisingCodes[] = {
    'A', 'B', 'B', 'C', 'F', 'G', 'I',
    'J', 'L', 'M', 'M', 'Q', 'T', 'Y'};
constexpr char kFallingCodes[] = {
    'B', 'D', 'H', 'O', 'R', 'U', 'V', 'X', 'X', 'Z'};
constexpr char kSteadyCodes[] = {
    'A', 'B', 'B', 'B', 'E', 'K', 'N', 'N', 'P',
    'P', 'S', 'W', 'W', 'X', 'X', 'X', 'Z'};

float clampFloat(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

template <size_t N>
char lookupCode(const char (&table)[N], float indexValue) {
  int index = static_cast<int>(floorf(indexValue + 0.5f));
  if (index < 0) index = 0;
  if (index >= static_cast<int>(N)) index = static_cast<int>(N) - 1;
  return table[index];
}

bool summerHalfYear(uint8_t month, bool northernHemisphere) {
  const bool northernSummer = month >= 4 && month <= 9;
  return northernHemisphere ? northernSummer : !northernSummer;
}
}  // namespace

Result forecast(float seaLevelPressureHpa, float trendHpaPerHour,
                uint8_t month, bool monthValid,
                float windDirectionDeg, bool windValid,
                bool northernHemisphere) {
  Result result;
  if (!isfinite(seaLevelPressureHpa) || !isfinite(trendHpaPerHour)) {
    return result;
  }

  float pressure = clampFloat(seaLevelPressureHpa,
                              kPressureBottomHpa, kPressureTopHpa);

  if (windValid && isfinite(windDirectionDeg)) {
    float normalized = fmodf(windDirectionDeg, 360.0f);
    if (normalized < 0.0f) normalized += 360.0f;
    int windIndex = static_cast<int>(floorf(normalized / 22.5f + 0.5f)) % 16;
    if (!northernHemisphere) windIndex = (windIndex + 8) % 16;
    pressure += kWindPressureAdjustmentHpa[windIndex];
    result.windUsed = true;
  }

  if (trendHpaPerHour >= kTrendThresholdHpaPerHour) {
    result.trend = Trend::Rising;
    if (monthValid && month >= 1 && month <= 12 &&
        summerHalfYear(month, northernHemisphere)) {
      pressure += 3.2f;
      result.seasonalCorrectionApplied = true;
    }
    result.code = lookupCode(kRisingCodes,
                             0.1740f * (1031.40f - pressure));
  } else if (trendHpaPerHour <= -kTrendThresholdHpaPerHour) {
    result.trend = Trend::Falling;
    if (monthValid && month >= 1 && month <= 12 &&
        summerHalfYear(month, northernHemisphere)) {
      pressure -= 3.2f;
      result.seasonalCorrectionApplied = true;
    }
    result.code = lookupCode(kFallingCodes,
                             0.1553f * (1029.95f - pressure));
  } else {
    result.trend = Trend::Steady;
    result.code = lookupCode(kSteadyCodes,
                             0.2314f * (1030.81f - pressure));
  }

  result.adjustedPressureHpa = pressure;
  result.valid = result.code >= 'A' && result.code <= 'Z';
  return result;
}

const char* forecastTextCs(char code) {
  switch (code) {
    case 'A': return "Ustalene jasno";
    case 'B': return "Pekne pocasi";
    case 'C': return "Vyjasnovani";
    case 'D': return "Pekne, pozdeji mene stale";
    case 'E': return "Pekne, mozne prehanky";
    case 'F': return "Pomerne pekne, zlepsovani";
    case 'G': return "Pekne, zkraje prehanky";
    case 'H': return "Pekne, pozdeji prehanky";
    case 'I': return "Zkraje prehanky, zlepseni";
    case 'J': return "Promenlivo, zlepsovani";
    case 'K': return "Pomerne pekne, prehanky";
    case 'L': return "Nestale, pozdeji vyjasneni";
    case 'M': return "Nestale, zrejme zlepseni";
    case 'N': return "Prehanky, jasne intervaly";
    case 'O': return "Prehanky, postupne horsi";
    case 'P': return "Promenlivo, misty dest";
    case 'Q': return "Nestale, kratka vyjasneni";
    case 'R': return "Nestale, pozdeji dest";
    case 'S': return "Nestale, misty dest";
    case 'T': return "Velmi nestale";
    case 'U': return "Obcasny dest, zhorsovani";
    case 'V': return "Dest, velmi nestale";
    case 'W': return "Casty dest";
    case 'X': return "Dest, velmi nestale";
    case 'Y': return "Bourlivo, mozne zlepseni";
    case 'Z': return "Bourlivo, vydatny dest";
    default: return "Predpoved neni k dispozici";
  }
}

const char* trendTextCs(Trend trend) {
  switch (trend) {
    case Trend::Rising: return "stoupa";
    case Trend::Falling: return "klesa";
    case Trend::Steady:
    default: return "staly";
  }
}

}  // namespace Zambretti
