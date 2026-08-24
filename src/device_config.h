#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>

#include "config.h"
#include "map_viewport.h"
#include "models.h"

constexpr size_t BACKLIGHT_DAY_COUNT = 7;
constexpr size_t WIFI_PROFILE_COUNT = 5;

enum class OtaDisplayEvent : uint8_t {
  Start,
  Progress,
  Success,
  Failure,
};

using OtaDisplayCallback = void (*)(OtaDisplayEvent event,
                                    const char* filename,
                                    uint32_t bytesWritten,
                                    int errorCode);

struct BacklightDaySchedule {
  bool enabled = true;
  uint16_t startMinutes = 6U * 60U;
  uint16_t endMinutes = 23U * 60U;
};

struct WifiProfile {
  bool enabled = false;
  String ssid;
  String password;
};

struct DeviceSettings {
  WifiProfile wifiProfiles[WIFI_PROFILE_COUNT];
  String wuApiKey;
  String wuStationId;
  String adsbUrl;
  bool localAdsbEnabled = false;
  float homeLat = Config::DEFAULT_HOME_LAT;
  float homeLon = Config::DEFAULT_HOME_LON;
  bool radarLayerEnabled = true;
  bool lightningLayerEnabled = true;
  bool adsbLayerEnabled = true;
  bool aircraftAlertEnabled = false;
  String aircraftAlertTargets[AIRCRAFT_ALERT_SLOT_COUNT];
  bool backlightScheduleEnabled = true;
  bool barometerEnabled = true;
  float barometerAltitudeM = 0.0f;
  float barometerOffsetHpa = 0.0f;
  BacklightDaySchedule backlightDays[BACKLIGHT_DAY_COUNT];
};

class DeviceConfigService {
 public:
  DeviceConfigService();

  void load();
  void begin(const AircraftSnapshot* aircraftSnapshot,
             const RuntimeDiagnostics* runtimeDiagnostics);
  void loop();
  bool connectStation(uint32_t timeoutMs);
  void ensureNetwork(uint32_t timeoutMs);
  // Non-blocking runtime reconnect state machine. Call every loop iteration;
  // it tries the enabled profiles without delay()/wait loops and starts the
  // configuration AP when a full cycle fails.
  void serviceNetwork();

  bool stationConnected() const;
  bool portalActive() const { return portalActive_; }
  String networkLabel() const;
  String accessUrl() const;
  const String& accessPointSsid() const { return accessPointSsid_; }
  const DeviceSettings& settings() const { return settings_; }
  AircraftAlertConfig alertConfig() const;
  bool consumeRuntimeSettingsChanged();
  bool consumeLcdResyncRequested();
  bool consumeMapZoomRequested(MapZoomMode& mode);
  bool otaInProgress() const { return otaInProgress_; }
  void setOtaDisplayCallback(OtaDisplayCallback callback) {
    otaDisplayCallback_ = callback;
  }

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
  void handleLcdResync();
  void handleMapZoom();
  void handleOtaPrepare();
  void handleOtaUpload();
  void handleOtaResult();
  void handleStatusJson();
  void handleDiagnostics();
  void handleDiagnosticsJson();
  void handleCaptivePortal();
  void sendErrorPage(const String& message, int code = 400);
  String buildPage() const;
  String buildDiagnosticsPage() const;
  bool alertTargetPresent(size_t slot) const;
  bool hasEnabledWifiProfile() const;
  size_t enabledWifiProfileCount() const;

  DeviceSettings settings_;
  const AircraftSnapshot* aircraftSnapshot_ = nullptr;
  const RuntimeDiagnostics* runtimeDiagnostics_ = nullptr;
  WebServer server_;
  DNSServer dnsServer_;
  bool serverStarted_ = false;
  bool portalActive_ = false;
  bool mdnsStarted_ = false;
  bool restartPending_ = false;
  bool runtimeSettingsChanged_ = false;
  bool lcdResyncRequested_ = false;
  bool mapZoomRequestPending_ = false;
  MapZoomMode requestedMapZoom_ = MapZoomMode::Full;
  bool otaInProgress_ = false;
  bool otaPrepareDisplayPending_ = false;
  bool otaDisplayFailurePending_ = false;
  OtaDisplayCallback otaDisplayCallback_ = nullptr;
  String otaFilename_;
  uint32_t otaExpectedBytes_ = 0;
  bool otaSucceeded_ = false;
  uint32_t otaBytesWritten_ = 0;
  int otaError_ = 0;
  uint32_t restartAt_ = 0;
  String accessPointSsid_;
  int8_t activeWifiProfile_ = -1;
  size_t nextWifiProfileIndex_ = 0;
  bool asyncReconnectActive_ = false;
  size_t asyncReconnectVisited_ = 0;
  size_t asyncReconnectIndex_ = 0;
  int8_t asyncReconnectProfile_ = -1;
  uint32_t asyncReconnectAttemptStartedMs_ = 0;
  uint32_t asyncReconnectNextCycleMs_ = 0;
};
