#pragma once

#include <Arduino.h>

#include "bmp180_shared_i2c.h"
#include "models.h"

class BarometerService {
 public:
  BarometerService();

  bool begin(bool enabled, float altitudeM, float offsetHpa);
  bool update(uint32_t nowMs);
  void configure(bool enabled, float altitudeM, float offsetHpa);
  void setWindDirection(float directionDeg);
  void setOutdoorTemperature(float temperatureC, uint32_t observationEpoch);
  void clearOutdoorTemperatureHistory();
  const BarometerSnapshot& snapshot() const { return snapshot_; }

 private:
  enum class SensorKind : uint8_t {
    None = 0,
    BMP180,
  };

  bool detectSensor();
  bool readSensor(float& pressureHpa, float& temperatureC);
  float toSeaLevelPressure(float stationPressureHpa,
                           float reductionTemperatureC) const;
  void clearHistory();
  void recordHistory(uint32_t nowMs, float pressureHpa,
                     float stationPressureHpa);
  void pruneOutdoorTemperatureHistory(uint32_t newestEpoch);
  bool outdoorTemperatureAverage(float& averageC, float& spanHours,
                                 size_t& sampleCount) const;
  float selectReductionTemperature();
  void calculateTrend(uint32_t nowMs);
  void updateText(uint32_t observedSpanMs);
  void updateZambretti(uint32_t observedSpanMs);

  struct OutdoorTemperatureSample {
    uint32_t epoch = 0;
    float temperatureC = NAN;
  };

  static constexpr size_t OUTDOOR_TEMPERATURE_SAMPLE_COUNT = 145;

  SensorKind kind_ = SensorKind::None;
  Bmp180SharedI2c bmp180_;
  BarometerSnapshot snapshot_;
  bool enabled_ = true;
  float altitudeM_ = 0.0f;
  float offsetHpa_ = 0.0f;
  float windDirectionDeg_ = NAN;
  OutdoorTemperatureSample outdoorTemperatureHistory_[
      OUTDOOR_TEMPERATURE_SAMPLE_COUNT];
  size_t outdoorTemperatureCount_ = 0;
  uint32_t lastOutdoorTemperatureEpoch_ = 0;
  uint32_t lastHistoryPointMs_ = 0;
};
