#pragma once

#include <Arduino.h>
#include "models.h"

class AdsbService {
 public:
  explicit AdsbService(const char* aircraftUrl);
  bool update();
  const AircraftSnapshot& snapshot() const { return snapshot_; }

 private:
  bool fetch();
  String aircraftUrl_;
  AircraftSnapshot snapshot_;
};
