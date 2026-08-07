#pragma once

#include <Arduino.h>
#include <driver/i2c.h>
#include <esp_err.h>

// Minimal BMP180 driver which reuses the ESP-IDF I2C controller already
// initialized by the Waveshare display/touch library. It intentionally does
// not call Wire.begin(), because reinitializing the shared GPIO8/GPIO9 bus can
// break GT911 touch and the CH422G IO expander.
class Bmp180SharedI2c {
 public:
  bool begin(i2c_port_t port = I2C_NUM_0, uint8_t address = 0x77);
  bool read(float& pressureHpa, float& temperatureC);

  bool ready() const { return ready_; }
  uint8_t chipId() const { return chipId_; }
  esp_err_t lastError() const { return lastError_; }
  const char* lastErrorName() const;

 private:
  struct Calibration {
    int16_t ac1 = 0;
    int16_t ac2 = 0;
    int16_t ac3 = 0;
    uint16_t ac4 = 0;
    uint16_t ac5 = 0;
    uint16_t ac6 = 0;
    int16_t b1 = 0;
    int16_t b2 = 0;
    int16_t mb = 0;
    int16_t mc = 0;
    int16_t md = 0;
  } calibration_;

  bool readRegisters(uint8_t reg, uint8_t* data, size_t length);
  bool writeRegister(uint8_t reg, uint8_t value);
  bool readCalibration();
  bool calibrationLooksValid() const;
  bool readRawTemperature(int32_t& value);
  bool readRawPressure(uint8_t oversampling, int32_t& value);
  bool compensate(int32_t uncompensatedTemperature,
                  int32_t uncompensatedPressure, uint8_t oversampling,
                  float& pressureHpa, float& temperatureC) const;

  i2c_port_t port_ = I2C_NUM_0;
  uint8_t address_ = 0x77;
  uint8_t chipId_ = 0;
  esp_err_t lastError_ = ESP_OK;
  bool ready_ = false;
};
