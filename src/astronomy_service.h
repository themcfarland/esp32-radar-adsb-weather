#pragma once

#include <Arduino.h>
#include "models.h"

class AstronomyService {
 public:
  bool update(float latitudeDeg, float longitudeDeg);
  const AstronomySnapshot& snapshot() const { return snapshot_; }

 private:
  AstronomySnapshot snapshot_;
  int lastLocalDateKey_ = -1;
  float lastLatitude_ = NAN;
  float lastLongitude_ = NAN;
};
