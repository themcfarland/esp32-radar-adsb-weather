#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>

#include "models.h"

struct DeviceSettings {
  String wifiSsid;
  String wifiPassword;
  String wuApiKey;
  String wuStationId;
  String adsbUrl;
  bool radarLayerEnabled = true;
  bool adsbLayerEnabled = true;
  bool aircraftAlertEnabled = false;
  String aircraftAlertTargets[AIRCRAFT_ALERT_SLOT_COUNT];
};

class DeviceConfigService {
 public:
  DeviceConfigService();

  void load();
  void begin(const AircraftSnapshot* aircraftSnapshot);
  void loop();
  bool connectStation(uint32_t timeoutMs);
  void ensureNetwork(uint32_t timeoutMs);

  bool stationConnected() const;
  bool portalActive() const { return portalActive_; }
  String networkLabel() const;
  String accessUrl() const;
  const String& accessPointSsid() const { return accessPointSsid_; }
  const DeviceSettings& settings() const { return settings_; }
  AircraftAlertConfig alertConfig() const;
  bool consumeRuntimeSettingsChanged();

 private:
  void startWebServer();
  void startAccessPoint(const char* reason);
  void stopAccessPoint();
  void startMdns();
  bool saveSettings();
  void handleRoot();
  void handleSave();
  void handleFactoryReset();
  void handleReboot();
  void handleStatusJson();
  void handleCaptivePortal();
  void sendErrorPage(const String& message, int code = 400);
  String buildPage() const;
  bool alertTargetPresent(size_t slot) const;

  DeviceSettings settings_;
  const AircraftSnapshot* aircraftSnapshot_ = nullptr;
  WebServer server_;
  DNSServer dnsServer_;
  bool serverStarted_ = false;
  bool portalActive_ = false;
  bool mdnsStarted_ = false;
  bool restartPending_ = false;
  bool runtimeSettingsChanged_ = false;
  uint32_t restartAt_ = 0;
  String accessPointSsid_;
};
