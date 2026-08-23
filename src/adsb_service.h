#pragma once

#include <Arduino.h>

#include "models.h"

class AdsbService {
 public:
  explicit AdsbService(const char* aircraftUrl);
  ~AdsbService();

  // Refreshes the local receiver every call and adsb.fi at its own slower
  // interval. The public snapshot is a de-duplicated merge where local
  // receiver data always has priority for the same Mode-S/ICAO hex address.
  bool update(bool includeNetwork = true);
  bool updateLocalNow() {
    nextLocalAttemptMs_ = 0;
    return update(false);
  }
  bool updateNetworkNow() {
    lastAdsbFiAttemptMs_ = 0;
    return update(true);
  }
  void setAircraftUrl(const String& aircraftUrl) {
    if (aircraftUrl_ == aircraftUrl) return;
    aircraftUrl_ = aircraftUrl;
    consecutiveLocalFailures_ = 0;
    nextLocalAttemptMs_ = 0;
    lastLocalSuccessMs_ = 0;
    if (localCache_) localCache_->valid = false;
  }
  void setLocalEnabled(bool enabled) {
    if (localEnabled_ == enabled) return;
    localEnabled_ = enabled;
    consecutiveLocalFailures_ = 0;
    nextLocalAttemptMs_ = 0;
    lastLocalSuccessMs_ = 0;
    if (localCache_) localCache_->valid = false;
  }
  bool localEnabled() const { return localEnabled_; }
  const AircraftSnapshot& snapshot() const { return snapshot_; }
  uint32_t lastSuccessMs() const;
  uint32_t lastLocalSuccessMs() const {
    return localEnabled_ ? lastLocalSuccessMs_ : 0U;
  }
  uint32_t lastNetworkSuccessMs() const { return lastAdsbFiSuccessMs_; }
  int lastNetworkHttpCode() const { return lastNetworkHttpCode_; }
  const char* networkSource() const { return networkSource_; }
  const char* networkStatus() const { return adsbFiStatus_; }

  // Network-worker integration. Background tasks fetch into private worker
  // instances; the main task then imports only the completed snapshot and
  // rebuilds the public merged view in a short, deterministic operation.
  void applyLocalSnapshot(const AircraftSnapshot& source);
  void applyNetworkSnapshot(const AircraftSnapshot& source);
  void refreshMergedSnapshot();

 private:
  bool ensureCaches();
  bool fetchLocal(AircraftSnapshot& target);
  bool fetchAdsbFi(AircraftSnapshot& target);
  bool fetchNetworkProvider(AircraftSnapshot& target, const char* providerLabel,
                            const char* host, const char* url);
  void mergeCaches(uint32_t nowMs);

  String aircraftUrl_;
  bool localEnabled_ = false;
  uint8_t consecutiveLocalFailures_ = 0;
  uint32_t nextLocalAttemptMs_ = 0;
  AircraftSnapshot* localCache_ = nullptr;
  AircraftSnapshot* adsbFiCache_ = nullptr;
  AircraftSnapshot snapshot_;
  uint32_t lastLocalSuccessMs_ = 0;
  uint32_t lastAdsbFiSuccessMs_ = 0;
  uint32_t lastAdsbFiAttemptMs_ = 0;
  int lastNetworkHttpCode_ = 0;
  char adsbFiStatus_[96] = "adsb.fi: waiting";
  char networkSource_[16] = "adsb.fi";
};
