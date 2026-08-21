#include "barometer_service.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "debug_log.h"
#include "zambretti_forecaster.h"

namespace {
constexpr uint32_t kTrendWindowMs = 3UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kMinimumTrendSpanMs =
    kTrendWindowMs - Config::PRESSURE_HISTORY_STEP_MS;
constexpr uint32_t kOutdoorTemperatureWindowSec = 12UL * 60UL * 60UL;
constexpr uint32_t kOutdoorTemperatureMaxAgeSec = 12UL * 60UL * 60UL;
constexpr float kStandardReductionTemperatureC = 15.0f;
constexpr uint8_t kForecastHours[3] = {3, 6, 9};

bool finitePressure(float value) {
  return isfinite(value) && value >= 250.0f && value <= 1250.0f;
}

bool finiteOutdoorTemperature(float value) {
  return isfinite(value) && value >= -60.0f && value <= 60.0f;
}

float clampFloat(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}
}  // namespace

BarometerService::BarometerService() = default;

bool BarometerService::begin(bool enabled, float altitudeM, float offsetHpa) {
  enabled_ = enabled;
  altitudeM_ = altitudeM;
  offsetHpa_ = offsetHpa;
  snapshot_.enabled = enabled_;

  if (!enabled_) {
    kind_ = SensorKind::None;
    snapshot_.detected = false;
    snapshot_.valid = false;
    strlcpy(snapshot_.sensorName, "vypnut", sizeof(snapshot_.sensorName));
    strlcpy(snapshot_.status, "Barometr je vypnut v nastaveni",
            sizeof(snapshot_.status));
    return false;
  }

  return detectSensor();
}

void BarometerService::setWindDirection(float directionDeg) {
  if (!isfinite(directionDeg)) {
    windDirectionDeg_ = NAN;
    return;
  }
  float normalized = fmodf(directionDeg, 360.0f);
  if (normalized < 0.0f) normalized += 360.0f;
  windDirectionDeg_ = normalized;
}

void BarometerService::pruneOutdoorTemperatureHistory(uint32_t newestEpoch) {
  if (outdoorTemperatureCount_ == 0 || newestEpoch == 0) return;
  const uint32_t cutoff =
      newestEpoch > kOutdoorTemperatureWindowSec
          ? newestEpoch - kOutdoorTemperatureWindowSec
          : 0;
  size_t removeCount = 0;
  while (removeCount < outdoorTemperatureCount_ &&
         outdoorTemperatureHistory_[removeCount].epoch < cutoff) {
    ++removeCount;
  }
  if (removeCount == 0) return;
  memmove(&outdoorTemperatureHistory_[0],
          &outdoorTemperatureHistory_[removeCount],
          (outdoorTemperatureCount_ - removeCount) *
              sizeof(OutdoorTemperatureSample));
  outdoorTemperatureCount_ -= removeCount;
}

void BarometerService::setOutdoorTemperature(float temperatureC,
                                              uint32_t observationEpoch) {
  if (!finiteOutdoorTemperature(temperatureC) ||
      observationEpoch < 1600000000UL) {
    return;
  }

  if (observationEpoch == lastOutdoorTemperatureEpoch_ &&
      outdoorTemperatureCount_ > 0) {
    outdoorTemperatureHistory_[outdoorTemperatureCount_ - 1].temperatureC =
        temperatureC;
    return;
  }
  if (lastOutdoorTemperatureEpoch_ != 0 &&
      observationEpoch < lastOutdoorTemperatureEpoch_) {
    return;
  }

  const bool firstOutdoorSample = outdoorTemperatureCount_ == 0;
  if (outdoorTemperatureCount_ >= OUTDOOR_TEMPERATURE_SAMPLE_COUNT) {
    memmove(&outdoorTemperatureHistory_[0],
            &outdoorTemperatureHistory_[1],
            (OUTDOOR_TEMPERATURE_SAMPLE_COUNT - 1) *
                sizeof(OutdoorTemperatureSample));
    outdoorTemperatureCount_ = OUTDOOR_TEMPERATURE_SAMPLE_COUNT - 1;
  }
  outdoorTemperatureHistory_[outdoorTemperatureCount_++] =
      {observationEpoch, temperatureC};
  lastOutdoorTemperatureEpoch_ = observationEpoch;
  pruneOutdoorTemperatureHistory(observationEpoch);

  // When the conversion changes from the 15 C fallback to real outdoor data,
  // discard sea-level graph points calculated with the previous basis. The
  // next barometer update immediately starts a consistent history; no flash
  // write is involved.
  if (firstOutdoorSample && snapshot_.historyCount > 0) {
    clearHistory();
    DebugLog::println(
        "Barometer: pressure history reset for first outdoor temperature");
  }
}

bool BarometerService::outdoorTemperatureAverage(float& averageC,
                                                  float& spanHours,
                                                  size_t& sampleCount) const {
  averageC = NAN;
  spanHours = 0.0f;
  sampleCount = 0;
  if (outdoorTemperatureCount_ == 0) return false;

  double sum = 0.0;
  for (size_t i = 0; i < outdoorTemperatureCount_; ++i) {
    if (!finiteOutdoorTemperature(
            outdoorTemperatureHistory_[i].temperatureC)) {
      continue;
    }
    sum += outdoorTemperatureHistory_[i].temperatureC;
    ++sampleCount;
  }
  if (sampleCount == 0) return false;
  averageC = static_cast<float>(sum / sampleCount);
  if (outdoorTemperatureCount_ > 1) {
    spanHours = static_cast<float>(
        outdoorTemperatureHistory_[outdoorTemperatureCount_ - 1].epoch -
        outdoorTemperatureHistory_[0].epoch) /
        3600.0f;
  }
  return true;
}

void BarometerService::clearOutdoorTemperatureHistory() {
  outdoorTemperatureCount_ = 0;
  lastOutdoorTemperatureEpoch_ = 0;
  snapshot_.wuTemperatureAverageC = NAN;
  snapshot_.wuTemperatureSampleCount = 0;
  snapshot_.wuTemperatureSpanHours = 0.0f;
  snapshot_.wuTemperatureLatestEpoch = 0;
  snapshot_.reductionTemperatureC = kStandardReductionTemperatureC;
  strlcpy(snapshot_.reductionTemperatureSource, "standard 15 C",
          sizeof(snapshot_.reductionTemperatureSource));
}

float BarometerService::selectReductionTemperature() {
  float averageC = NAN;
  float spanHours = 0.0f;
  size_t sampleCount = 0;
  const bool averageValid =
      outdoorTemperatureAverage(averageC, spanHours, sampleCount);

  bool recent = averageValid;
  const time_t nowEpoch = time(nullptr);
  if (recent && nowEpoch > 1700000000 && lastOutdoorTemperatureEpoch_ > 0) {
    const uint32_t now = static_cast<uint32_t>(nowEpoch);
    recent = now <= lastOutdoorTemperatureEpoch_ ||
             now - lastOutdoorTemperatureEpoch_ <=
                 kOutdoorTemperatureMaxAgeSec;
  }

  snapshot_.wuTemperatureAverageC = averageValid ? averageC : NAN;
  snapshot_.wuTemperatureSampleCount =
      static_cast<uint16_t>(sampleCount);
  snapshot_.wuTemperatureSpanHours = spanHours;
  snapshot_.wuTemperatureLatestEpoch = lastOutdoorTemperatureEpoch_;

  if (recent) {
    strlcpy(snapshot_.reductionTemperatureSource, "venkovni prumer 12 h",
            sizeof(snapshot_.reductionTemperatureSource));
    snapshot_.reductionTemperatureC = averageC;
    return averageC;
  }

  strlcpy(snapshot_.reductionTemperatureSource, "standard 15 C",
          sizeof(snapshot_.reductionTemperatureSource));
  snapshot_.reductionTemperatureC = kStandardReductionTemperatureC;
  return kStandardReductionTemperatureC;
}

void BarometerService::configure(bool enabled, float altitudeM,
                                 float offsetHpa) {
  const bool conversionChanged = fabsf(altitudeM - altitudeM_) > 0.01f ||
                                 fabsf(offsetHpa - offsetHpa_) > 0.001f;
  const bool enableChanged = enabled != enabled_;
  altitudeM_ = altitudeM;
  offsetHpa_ = offsetHpa;
  enabled_ = enabled;
  snapshot_.enabled = enabled_;

  if (conversionChanged || enableChanged) clearHistory();
  if (enableChanged || (enabled_ && !snapshot_.detected)) {
    begin(enabled_, altitudeM_, offsetHpa_);
  }
}

bool BarometerService::detectSensor() {
  kind_ = SensorKind::None;
  snapshot_.detected = false;
  snapshot_.valid = false;
  snapshot_.i2cAddress = 0;
  strlcpy(snapshot_.sensorName, "nenalezen", sizeof(snapshot_.sensorName));

  // The Waveshare library initializes GPIO8/GPIO9 with the ESP-IDF I2C
  // driver. Starting a second Arduino Wire bus here would attempt to reinstall the same
  // controller and can break GT911 touch or the CH422G expander. The BMP180
  // driver therefore talks directly through the already active I2C0 bus.
  if (bmp180_.begin(I2C_NUM_0, 0x77)) {
    kind_ = SensorKind::BMP180;
    snapshot_.detected = true;
    snapshot_.i2cAddress = 0x77;
    strlcpy(snapshot_.sensorName, "BMP180", sizeof(snapshot_.sensorName));
    snprintf(snapshot_.status, sizeof(snapshot_.status),
             "BMP180 nalezen na sdilene I2C0 0x77");
    DebugLog::printf(
        "Barometer: BMP180 detected on shared ESP-IDF I2C0 at 0x77, chip ID 0x%02X\n",
        static_cast<unsigned>(bmp180_.chipId()));
    return true;
  }

  const uint8_t observedId = bmp180_.chipId();
  if (observedId == 0x55) {
    snprintf(snapshot_.status, sizeof(snapshot_.status),
             "BMP180 ID 0x55, ale selhala kalibrace nebo prvni mereni");
  } else if (observedId != 0) {
    snprintf(snapshot_.status, sizeof(snapshot_.status),
             "I2C 0x77 odpovida, ale ID 0x%02X neni BMP180",
             static_cast<unsigned>(observedId));
  } else {
    snprintf(snapshot_.status, sizeof(snapshot_.status),
             "BMP180 bez odpovedi na sdilene I2C0 0x77 (%s)",
             bmp180_.lastErrorName());
  }
  DebugLog::printf(
      "Barometer: BMP180 not detected on shared ESP-IDF I2C0 0x77, chip ID 0x%02X, error=%s\n",
      static_cast<unsigned>(observedId), bmp180_.lastErrorName());
  return false;
}

bool BarometerService::readSensor(float& pressureHpa, float& temperatureC) {
  pressureHpa = NAN;
  temperatureC = NAN;

  switch (kind_) {
    case SensorKind::BMP180:
      if (!bmp180_.read(pressureHpa, temperatureC)) {
        snprintf(snapshot_.status, sizeof(snapshot_.status),
                 "BMP180 chyba cteni: %s", bmp180_.lastErrorName());
        return false;
      }
      break;
    case SensorKind::None:
    default:
      return false;
  }

  return finitePressure(pressureHpa) && isfinite(temperatureC);
}

float BarometerService::toSeaLevelPressure(
    float stationPressureHpa, float reductionTemperatureC) const {
  if (!finitePressure(stationPressureHpa)) return NAN;
  const float altitude = clampFloat(altitudeM_, -500.0f, 5000.0f);
  const float temperature = finiteOutdoorTemperature(reductionTemperatureC)
                                ? reductionTemperatureC
                                : kStandardReductionTemperatureC;
  constexpr float lapseRate = 0.0065f;
  const float denominator =
      temperature + lapseRate * altitude + 273.15f;
  if (denominator <= 1.0f) return stationPressureHpa + offsetHpa_;
  const float base = 1.0f - (lapseRate * altitude) / denominator;
  if (base <= 0.1f) return stationPressureHpa + offsetHpa_;
  return stationPressureHpa * powf(base, -5.257f) + offsetHpa_;
}

void BarometerService::clearHistory() {
  snapshot_.historyCount = 0;
  snapshot_.historyRevision++;
  snapshot_.trendHpaPerHour = 0.0f;
  snapshot_.delta3hHpa = NAN;
  snapshot_.zambrettiReady = false;
  snapshot_.zambrettiWindUsed = false;
  snapshot_.zambrettiSeasonApplied = false;
  snapshot_.zambrettiAdjustedPressureHpa = NAN;
  snapshot_.zambrettiWindDirectionDeg = NAN;
  strlcpy(snapshot_.zambrettiCode, "-", sizeof(snapshot_.zambrettiCode));
  strlcpy(snapshot_.zambrettiTrend, "sbiram",
          sizeof(snapshot_.zambrettiTrend));
  for (float& projected : snapshot_.projectedPressureHpa) projected = NAN;
  lastHistoryPointMs_ = 0;
  strlcpy(snapshot_.trendText, "sbiram data", sizeof(snapshot_.trendText));
  strlcpy(snapshot_.forecastText, "Zambretti zatim neni k dispozici",
          sizeof(snapshot_.forecastText));
}

void BarometerService::recordHistory(uint32_t nowMs, float pressureHpa,
                                     float stationPressureHpa) {
  if (!finitePressure(pressureHpa) || !finitePressure(stationPressureHpa)) {
    return;
  }

  if (snapshot_.historyCount == 0) {
    snapshot_.history[0] = {nowMs, pressureHpa, stationPressureHpa};
    snapshot_.historyCount = 1;
    snapshot_.historyRevision++;
    lastHistoryPointMs_ = nowMs;
    return;
  }

  if (static_cast<uint32_t>(nowMs - lastHistoryPointMs_) <
      Config::PRESSURE_HISTORY_STEP_MS) {
    // Keep the timestamp of an accepted history point immutable. Updating it
    // every minute would compress each nominal five-minute interval to about
    // one minute and distort the three-hour regression. The live pressure is
    // already available separately in snapshot_.pressureHpa.
    return;
  }

  if (snapshot_.historyCount >= PRESSURE_HISTORY_POINT_COUNT) {
    memmove(&snapshot_.history[0], &snapshot_.history[1],
            (PRESSURE_HISTORY_POINT_COUNT - 1) * sizeof(PressurePoint));
    snapshot_.historyCount = PRESSURE_HISTORY_POINT_COUNT - 1;
  }

  snapshot_.history[snapshot_.historyCount++] =
      {nowMs, pressureHpa, stationPressureHpa};
  snapshot_.historyRevision++;
  lastHistoryPointMs_ = nowMs;
}

void BarometerService::calculateTrend(uint32_t nowMs) {
  snapshot_.delta3hHpa = NAN;
  for (float& projected : snapshot_.projectedPressureHpa) projected = NAN;
  if (snapshot_.historyCount < 2) {
    snapshot_.trendHpaPerHour = 0.0f;
    updateText(0);
    return;
  }

  size_t first = 0;
  while (first + 1 < snapshot_.historyCount &&
         static_cast<uint32_t>(nowMs - snapshot_.history[first].timestampMs) >
             kTrendWindowMs) {
    ++first;
  }

  const size_t count = snapshot_.historyCount - first;
  if (count < 2) {
    updateText(0);
    return;
  }

  const uint32_t firstTimestamp = snapshot_.history[first].timestampMs;
  const uint32_t observedSpanMs =
      static_cast<uint32_t>(nowMs - firstTimestamp);

  double sumX = 0.0;
  double sumY = 0.0;
  double sumXX = 0.0;
  double sumXY = 0.0;
  size_t validCount = 0;
  for (size_t i = first; i < snapshot_.historyCount; ++i) {
    const float pressure = snapshot_.history[i].stationPressureHpa;
    if (!finitePressure(pressure)) continue;
    const double xHours =
        static_cast<uint32_t>(snapshot_.history[i].timestampMs - firstTimestamp) /
        3600000.0;
    sumX += xHours;
    sumY += pressure;
    sumXX += xHours * xHours;
    sumXY += xHours * pressure;
    ++validCount;
  }

  const double denominator = validCount * sumXX - sumX * sumX;
  if (validCount < 2 || fabs(denominator) < 1e-9) {
    snapshot_.trendHpaPerHour = 0.0f;
    updateText(observedSpanMs);
    return;
  }

  const float slope = static_cast<float>(
      (validCount * sumXY - sumX * sumY) / denominator);
  snapshot_.trendHpaPerHour = clampFloat(slope, -3.0f, 3.0f);

  if (observedSpanMs >= kMinimumTrendSpanMs) {
    snapshot_.delta3hHpa = snapshot_.trendHpaPerHour * 3.0f;
    const float projectionSlope =
        clampFloat(snapshot_.trendHpaPerHour, -1.2f, 1.2f);
    for (size_t i = 0; i < 3; ++i) {
      snapshot_.projectedPressureHpa[i] =
          snapshot_.pressureHpa + projectionSlope * kForecastHours[i];
    }
  }
  updateText(observedSpanMs);
}

void BarometerService::updateText(uint32_t observedSpanMs) {
  if (observedSpanMs < kMinimumTrendSpanMs) {
    strlcpy(snapshot_.trendText, "sbiram 3h trend",
            sizeof(snapshot_.trendText));
    snprintf(snapshot_.forecastText, sizeof(snapshot_.forecastText),
             "Zambretti po %u min dat",
             static_cast<unsigned>(kMinimumTrendSpanMs / 60000UL));
    updateZambretti(observedSpanMs);
    return;
  }

  if (!isfinite(snapshot_.delta3hHpa)) {
    strlcpy(snapshot_.trendText, "trend neni platny",
            sizeof(snapshot_.trendText));
    strlcpy(snapshot_.forecastText, "Zambretti ceka na platny trend",
            sizeof(snapshot_.forecastText));
    updateZambretti(0);
    return;
  }

  // Five descriptive trend bands follow the practical thresholds used by the
  // referenced pressure-trend guide. Zambretti itself uses its classic
  // rising/steady/falling classification in updateZambretti().
  const float delta = snapshot_.delta3hHpa;
  if (delta < -2.0f) {
    strlcpy(snapshot_.trendText, "rychly pokles",
            sizeof(snapshot_.trendText));
  } else if (delta <= -0.5f) {
    strlcpy(snapshot_.trendText, "pokles", sizeof(snapshot_.trendText));
  } else if (delta < 0.5f) {
    strlcpy(snapshot_.trendText, "stabilni", sizeof(snapshot_.trendText));
  } else if (delta <= 2.0f) {
    strlcpy(snapshot_.trendText, "vzestup", sizeof(snapshot_.trendText));
  } else {
    strlcpy(snapshot_.trendText, "rychly vzestup",
            sizeof(snapshot_.trendText));
  }
  updateZambretti(observedSpanMs);
}

void BarometerService::updateZambretti(uint32_t observedSpanMs) {
  snapshot_.zambrettiReady = false;
  snapshot_.zambrettiWindUsed = false;
  snapshot_.zambrettiSeasonApplied = false;
  snapshot_.zambrettiAdjustedPressureHpa = NAN;
  snapshot_.zambrettiWindDirectionDeg = NAN;
  strlcpy(snapshot_.zambrettiCode, "-", sizeof(snapshot_.zambrettiCode));
  strlcpy(snapshot_.zambrettiTrend, "sbiram",
          sizeof(snapshot_.zambrettiTrend));

  if (observedSpanMs < kMinimumTrendSpanMs ||
      !finitePressure(snapshot_.pressureHpa)) {
    return;
  }

  uint8_t month = 0;
  bool monthValid = false;
  const time_t epoch = time(nullptr);
  if (epoch > 1700000000) {
    struct tm localTime {};
    localtime_r(&epoch, &localTime);
    month = static_cast<uint8_t>(localTime.tm_mon + 1);
    monthValid = true;
  }

  const bool windValid = isfinite(windDirectionDeg_);
  const Zambretti::Result result = Zambretti::forecast(
      snapshot_.pressureHpa, snapshot_.trendHpaPerHour,
      month, monthValid, windDirectionDeg_, windValid, true);
  if (!result.valid) return;

  snapshot_.zambrettiReady = true;
  snapshot_.zambrettiWindUsed = result.windUsed;
  snapshot_.zambrettiSeasonApplied = result.seasonalCorrectionApplied;
  snapshot_.zambrettiAdjustedPressureHpa = result.adjustedPressureHpa;
  snapshot_.zambrettiWindDirectionDeg =
      result.windUsed ? windDirectionDeg_ : NAN;
  snapshot_.zambrettiCode[0] = result.code;
  snapshot_.zambrettiCode[1] = '\0';
  strlcpy(snapshot_.zambrettiTrend,
          Zambretti::trendTextCs(result.trend),
          sizeof(snapshot_.zambrettiTrend));
  strlcpy(snapshot_.forecastText,
          Zambretti::forecastTextCs(result.code),
          sizeof(snapshot_.forecastText));
}

bool BarometerService::update(uint32_t nowMs) {
  if (!enabled_) return false;
  if (!snapshot_.detected && !detectSensor()) return false;

  float rawPressure = NAN;
  float temperature = NAN;
  if (!readSensor(rawPressure, temperature)) {
    snapshot_.valid = false;
    snprintf(snapshot_.status, sizeof(snapshot_.status),
             "%s: chyba mereni", snapshot_.sensorName);
    return false;
  }

  const float reductionTemperature = selectReductionTemperature();
  const float corrected =
      toSeaLevelPressure(rawPressure, reductionTemperature);
  if (!finitePressure(corrected)) {
    snapshot_.valid = false;
    strlcpy(snapshot_.status, "Barometr: neplatny prepocet tlaku",
            sizeof(snapshot_.status));
    return false;
  }

  snapshot_.rawPressureHpa = rawPressure;
  snapshot_.pressureHpa = corrected;
  snapshot_.temperatureC = temperature;
  snapshot_.valid = true;
  snapshot_.sampleCount++;
  recordHistory(nowMs, corrected, rawPressure);
  calculateTrend(nowMs);
  snprintf(snapshot_.status, sizeof(snapshot_.status),
           "%s 0x%02X: %.1f hPa, Tred %.1f C (%s), trend %+.2f hPa/h, Z=%s",
           snapshot_.sensorName, static_cast<unsigned>(snapshot_.i2cAddress),
           corrected, reductionTemperature,
           snapshot_.reductionTemperatureSource,
           snapshot_.trendHpaPerHour, snapshot_.zambrettiCode);
  return true;
}
