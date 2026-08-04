#pragma once

#include <Arduino.h>
#include "models.h"

class AdsbService {
 public:
  explicit AdsbService(const char* aircraftUrl);
  bool update();
  void setAircraftUrl(const String& aircraftUrl) { aircraftUrl_ = aircraftUrl; }
  const AircraftSnapshot& snapshot() const { return snapshot_; }

 private:
  bool fetch();
  String aircraftUrl_;
  AircraftSnapshot snapshot_;
};
