#include "bmp180_shared_i2c.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

namespace {
constexpr uint8_t kBmp180ChipIdRegister = 0xD0;
constexpr uint8_t kBmp180ExpectedChipId = 0x55;
constexpr uint8_t kCalibrationStartRegister = 0xAA;
constexpr uint8_t kControlRegister = 0xF4;
constexpr uint8_t kDataRegister = 0xF6;
constexpr uint8_t kTemperatureCommand = 0x2E;
constexpr uint8_t kPressureCommand = 0x34;
constexpr uint8_t kOversampling = 3;
constexpr TickType_t kI2cTimeoutTicks = pdMS_TO_TICKS(80);

int16_t signedBigEndian(const uint8_t* data) {
  return static_cast<int16_t>((static_cast<uint16_t>(data[0]) << 8) |
                              static_cast<uint16_t>(data[1]));
}

uint16_t unsignedBigEndian(const uint8_t* data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) |
                               static_cast<uint16_t>(data[1]));
}
}  // namespace

bool Bmp180SharedI2c::readRegisters(uint8_t reg, uint8_t* data,
                                    size_t length) {
  if (!data || length == 0) {
    lastError_ = ESP_ERR_INVALID_ARG;
    return false;
  }
  lastError_ = i2c_master_write_read_device(
      port_, address_, &reg, 1, data, length, kI2cTimeoutTicks);
  return lastError_ == ESP_OK;
}

bool Bmp180SharedI2c::writeRegister(uint8_t reg, uint8_t value) {
  const uint8_t payload[2] = {reg, value};
  lastError_ = i2c_master_write_to_device(port_, address_, payload,
                                          sizeof(payload),
                                          kI2cTimeoutTicks);
  return lastError_ == ESP_OK;
}

bool Bmp180SharedI2c::readCalibration() {
  uint8_t data[22] = {0};
  if (!readRegisters(kCalibrationStartRegister, data, sizeof(data))) {
    return false;
  }

  calibration_.ac1 = signedBigEndian(&data[0]);
  calibration_.ac2 = signedBigEndian(&data[2]);
  calibration_.ac3 = signedBigEndian(&data[4]);
  calibration_.ac4 = unsignedBigEndian(&data[6]);
  calibration_.ac5 = unsignedBigEndian(&data[8]);
  calibration_.ac6 = unsignedBigEndian(&data[10]);
  calibration_.b1 = signedBigEndian(&data[12]);
  calibration_.b2 = signedBigEndian(&data[14]);
  calibration_.mb = signedBigEndian(&data[16]);
  calibration_.mc = signedBigEndian(&data[18]);
  calibration_.md = signedBigEndian(&data[20]);
  return calibrationLooksValid();
}

bool Bmp180SharedI2c::calibrationLooksValid() const {
  // Disconnected buses commonly return all zeroes or all 0xFF. AC4..AC6 are
  // unsigned factory constants and must be non-zero for compensation.
  if (calibration_.ac4 == 0 || calibration_.ac4 == 0xFFFF ||
      calibration_.ac5 == 0 || calibration_.ac5 == 0xFFFF ||
      calibration_.ac6 == 0 || calibration_.ac6 == 0xFFFF) {
    return false;
  }
  const bool allSignedZero =
      calibration_.ac1 == 0 && calibration_.ac2 == 0 &&
      calibration_.ac3 == 0 && calibration_.b1 == 0 &&
      calibration_.b2 == 0 && calibration_.mb == 0 &&
      calibration_.mc == 0 && calibration_.md == 0;
  return !allSignedZero;
}

bool Bmp180SharedI2c::begin(i2c_port_t port, uint8_t address) {
  ready_ = false;
  chipId_ = 0;
  port_ = port;
  address_ = address;

  // Cheap BMP180 modules can need a short settling time after the LCD board
  // enables its shared I2C peripherals. Retry without reinstalling the bus.
  for (uint8_t attempt = 0; attempt < 4; ++attempt) {
    uint8_t id = 0;
    if (readRegisters(kBmp180ChipIdRegister, &id, 1)) {
      chipId_ = id;
      if (id == kBmp180ExpectedChipId && readCalibration()) {
        int32_t rawTemperature = 0;
        int32_t rawPressure = 0;
        float pressureHpa = NAN;
        float temperatureC = NAN;
        if (readRawTemperature(rawTemperature) &&
            readRawPressure(kOversampling, rawPressure) &&
            compensate(rawTemperature, rawPressure, kOversampling,
                       pressureHpa, temperatureC) &&
            isfinite(pressureHpa) && pressureHpa >= 250.0f &&
            pressureHpa <= 1250.0f && isfinite(temperatureC)) {
          ready_ = true;
          return true;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(40));
  }
  return false;
}

bool Bmp180SharedI2c::readRawTemperature(int32_t& value) {
  if (!writeRegister(kControlRegister, kTemperatureCommand)) return false;
  vTaskDelay(pdMS_TO_TICKS(5));
  uint8_t data[2] = {0};
  if (!readRegisters(kDataRegister, data, sizeof(data))) return false;
  value = static_cast<int32_t>(unsignedBigEndian(data));
  return true;
}

bool Bmp180SharedI2c::readRawPressure(uint8_t oversampling, int32_t& value) {
  oversampling = oversampling > 3 ? 3 : oversampling;
  const uint8_t command =
      static_cast<uint8_t>(kPressureCommand + (oversampling << 6));
  if (!writeRegister(kControlRegister, command)) return false;

  static constexpr uint8_t conversionDelayMs[4] = {5, 8, 14, 26};
  vTaskDelay(pdMS_TO_TICKS(conversionDelayMs[oversampling]));
  uint8_t data[3] = {0};
  if (!readRegisters(kDataRegister, data, sizeof(data))) return false;
  value = static_cast<int32_t>(
      ((static_cast<uint32_t>(data[0]) << 16) |
       (static_cast<uint32_t>(data[1]) << 8) |
       static_cast<uint32_t>(data[2])) >>
      (8 - oversampling));
  return true;
}

bool Bmp180SharedI2c::compensate(int32_t ut, int32_t up,
                                 uint8_t oversampling,
                                 float& pressureHpa,
                                 float& temperatureC) const {
  const int64_t x1t =
      ((static_cast<int64_t>(ut) - calibration_.ac6) * calibration_.ac5) >>
      15;
  const int64_t denominator = x1t + calibration_.md;
  if (denominator == 0) return false;
  const int64_t x2t =
      (static_cast<int64_t>(calibration_.mc) << 11) / denominator;
  const int64_t b5 = x1t + x2t;
  temperatureC = static_cast<float>((b5 + 8) >> 4) / 10.0f;

  const int64_t b6 = b5 - 4000;
  int64_t x1 = (static_cast<int64_t>(calibration_.b2) *
                ((b6 * b6) >> 12)) >>
               11;
  int64_t x2 = (static_cast<int64_t>(calibration_.ac2) * b6) >> 11;
  int64_t x3 = x1 + x2;
  const int64_t b3 =
      (((static_cast<int64_t>(calibration_.ac1) * 4 + x3)
        << oversampling) +
       2) >>
      2;

  x1 = (static_cast<int64_t>(calibration_.ac3) * b6) >> 13;
  x2 = (static_cast<int64_t>(calibration_.b1) * ((b6 * b6) >> 12)) >> 16;
  x3 = (x1 + x2 + 2) >> 2;
  const uint64_t b4 =
      (static_cast<uint64_t>(calibration_.ac4) *
       static_cast<uint64_t>(x3 + 32768)) >>
      15;
  if (b4 == 0 || up <= b3) return false;

  const uint64_t b7 =
      static_cast<uint64_t>(up - b3) * (50000U >> oversampling);
  int64_t pressurePa =
      b7 < 0x80000000ULL
          ? static_cast<int64_t>((b7 * 2ULL) / b4)
          : static_cast<int64_t>((b7 / b4) * 2ULL);

  x1 = pressurePa >> 8;
  x1 = (x1 * x1 * 3038) >> 16;
  x2 = (-7357 * pressurePa) >> 16;
  pressurePa += (x1 + x2 + 3791) >> 4;

  pressureHpa = static_cast<float>(pressurePa) / 100.0f;
  return isfinite(pressureHpa) && isfinite(temperatureC);
}

bool Bmp180SharedI2c::read(float& pressureHpa, float& temperatureC) {
  pressureHpa = NAN;
  temperatureC = NAN;
  if (!ready_) return false;

  int32_t rawTemperature = 0;
  int32_t rawPressure = 0;
  if (!readRawTemperature(rawTemperature) ||
      !readRawPressure(kOversampling, rawPressure) ||
      !compensate(rawTemperature, rawPressure, kOversampling, pressureHpa,
                  temperatureC)) {
    return false;
  }
  return pressureHpa >= 250.0f && pressureHpa <= 1250.0f &&
         temperatureC >= -60.0f && temperatureC <= 85.0f;
}

const char* Bmp180SharedI2c::lastErrorName() const {
  return esp_err_to_name(lastError_);
}
