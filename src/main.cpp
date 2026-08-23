#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Waveshare_ST7262_LVGL.h>
#include <esp_system.h>
#include <esp_lcd_panel_rgb.h>
#include <time.h>

#include "adsb_service.h"
#include "astronomy_service.h"
#include "barometer_service.h"
#include "config.h"
#include "debug_log.h"
#include "device_config.h"
#include "map_renderer.h"
#include "network_worker.h"
#include "lightning_service.h"
#include "radar_service.h"
#include "ui.h"
#include "weather_service.h"
#include "version.h"

AdsbService adsb("");
AstronomyService astronomy;
BarometerService barometer;
WeatherService weather("", "");
RadarService radar;
LightningService lightning;
DeviceConfigService deviceConfig;
NetworkWorker networkWorker;
AircraftAlertConfig aircraftAlert;
bool radarLayerEnabled = true;
bool lightningLayerEnabled = true;
bool adsbLayerEnabled = true;
bool lightningProximityAlertActive = false;
float homeLat = Config::DEFAULT_HOME_LAT;
float homeLon = Config::DEFAULT_HOME_LON;
Preferences mapPreferences;
MapViewport mapViewport;
RuntimeDiagnostics runtimeDiagnostics;

uint8_t radarFrame = 0;
uint32_t lastAdsbUpdate = 0;
uint32_t lastRadarUpdate = 0;
uint32_t lastAdsbLocalRequest = 0;
uint32_t lastAdsbInternetRequest = 0;
uint32_t lastRadarRequest = 0;
uint32_t lastCurrentWeatherRequest = 0;
uint32_t lastForecastRequest = 0;
uint32_t lastAdsbMerge = 0;
uint32_t lastLightningUpdate = 0;
uint32_t lastRadarAnimation = 0;
uint32_t lastCurrentWeatherUpdate = 0;
uint32_t lastForecastUpdate = 0;
uint32_t lastAstronomyUpdate = 0;
uint32_t lastBarometerUpdate = 0;
uint32_t lastHeaderUpdate = 0;
uint32_t lastDebugHeartbeat = 0;
uint32_t lastDisplaySyncRecovery = 0;
bool mapDirty = true;
bool mapViewSavePending = false;
bool mapPreferencesReady = false;
bool displayResyncPending = false;
bool lastNetworkConnected = false;
uint32_t lastGlobalNetworkRecovery = 0;
uint32_t lastMapViewChange = 0;
uint32_t lcdResyncCount = 0;
uint32_t lcdLoadGuardTriggerCount = 0;
uint32_t longestLoopDurationMs = 0;
uint32_t lastNetworkCompletedSeen = 0;
uint32_t lastNetworkFailedSeen = 0;
uint32_t mapRedrawCount = 0;
uint32_t lastMapRedrawDurationMs = 0;
int lcdBacklightState = 1;
bool backlightOn = true;
bool backlightScheduledWindowActive = true;
bool backlightTemporaryWake = false;
uint32_t backlightWakeUntil = 0;
bool barometerInitialized = false;
String barometerWuStationId;
float barometerWeatherLat = NAN;
float barometerWeatherLon = NAN;
bool startupScreenActive = false;
bool otaScreenActive = false;
uint32_t otaScreenDismissAt = 0;

namespace {
void startupStatus(const char* message, uint8_t progressPercent) {
  if (!startupScreenActive) return;
  lvgl_port_lock(-1);
  UI::updateStartupStatus(message, progressPercent);
  lvgl_port_unlock();
  DebugLog::printf("STARTUP %u%%: %s\n",
                   static_cast<unsigned>(progressPercent),
                   message ? message : "");
  delay(12);
}

bool updateBarometerSample(uint32_t nowMs) {
  const CurrentWeather& current = weather.snapshot().current;
  barometer.setWindDirection(current.valid ? current.windDirectionDeg : NAN);
  if (current.valid) {
    barometer.setOutdoorTemperature(current.temperatureC, current.epoch);
  }

  // GT911 is polled by the LVGL task on the same physical I2C bus. Holding
  // the LVGL mutex pauses touch transactions while the BMP180 performs its
  // temperature and pressure conversions.
  if (!lvgl_port_lock(500)) {
    DebugLog::println("Barometer: shared I2C lock timeout");
    return false;
  }
  const bool updated = barometer.update(nowMs);
  lvgl_port_unlock();
  return updated;
}

void applyDeviceSettings() {
  const DeviceSettings& settings = deviceConfig.settings();
  adsb.setAircraftUrl(settings.adsbUrl);
  adsb.setLocalEnabled(settings.localAdsbEnabled);
  homeLat = settings.homeLat;
  homeLon = settings.homeLon;
  weather.setConfig(settings.wuApiKey, settings.wuStationId);
  weather.setLocation(homeLat, homeLon);
  networkWorker.configure(settings.adsbUrl, settings.localAdsbEnabled,
                          settings.wuApiKey, settings.wuStationId,
                          homeLat, homeLon);
  const bool weatherLocationChanged =
      !isfinite(barometerWeatherLat) || !isfinite(barometerWeatherLon) ||
      fabsf(barometerWeatherLat - homeLat) > 0.00001f ||
      fabsf(barometerWeatherLon - homeLon) > 0.00001f;
  if (barometerWuStationId != settings.wuStationId || weatherLocationChanged) {
    barometer.clearOutdoorTemperatureHistory();
    barometerWuStationId = settings.wuStationId;
    barometerWeatherLat = homeLat;
    barometerWeatherLon = homeLon;
    DebugLog::println("Barometer: outdoor temperature history cleared after weather location/source change");
  }
  aircraftAlert = deviceConfig.alertConfig();
  radarLayerEnabled = settings.radarLayerEnabled;
  lightningLayerEnabled = settings.lightningLayerEnabled;
  adsbLayerEnabled = settings.adsbLayerEnabled;
  if (barometerInitialized) {
    lvgl_port_lock(-1);
    barometer.configure(settings.barometerEnabled,
                        settings.barometerAltitudeM,
                        settings.barometerOffsetHpa);
    lvgl_port_unlock();
  }
  DebugLog::printf(
      "Runtime config: HOME=%.5f,%.5f localADSB=%s ADSB=%s WU=%s layers=radar:%d lightning:%d adsb:%d alerts=%s [%s|%s|%s] backlightSchedule=%s barometer=%s\n",
      homeLat, homeLon, settings.localAdsbEnabled ? "on" : "off",
      settings.adsbUrl.c_str(), settings.wuStationId.c_str(),
      radarLayerEnabled ? 1 : 0, lightningLayerEnabled ? 1 : 0,
      adsbLayerEnabled ? 1 : 0,
      aircraftAlert.enabled ? "on" : "off", aircraftAlert.targets[0],
      aircraftAlert.targets[1], aircraftAlert.targets[2],
      settings.backlightScheduleEnabled ? "on" : "off",
      settings.barometerEnabled ? "on" : "off");
}

MapZoomMode storedZoomMode(uint8_t raw) {
  switch (raw) {
    case static_cast<uint8_t>(MapZoomMode::Km50):
      return MapZoomMode::Km50;
    case static_cast<uint8_t>(MapZoomMode::Km25):
      return MapZoomMode::Km25;
    case static_cast<uint8_t>(MapZoomMode::Km10):
      return MapZoomMode::Km10;
    case static_cast<uint8_t>(MapZoomMode::Full):
    default:
      return MapZoomMode::Full;
  }
}

void loadMapViewport() {
  mapPreferencesReady = mapPreferences.begin("mapview", false);
  if (!mapPreferencesReady) {
    mapViewport = MapRenderer::makeViewport(
        MapZoomMode::Full, homeLat, homeLon,
        Config::MAP_W, Config::MAP_H);
    DebugLog::println("Map view: NVS unavailable, using full Czech Republic");
    return;
  }

  const MapZoomMode mode =
      storedZoomMode(mapPreferences.getUChar("mode", 0));
  const float centerLat =
      mapPreferences.getFloat("lat", homeLat);
  const float centerLon =
      mapPreferences.getFloat("lon", homeLon);
  mapViewport = MapRenderer::makeViewport(mode, centerLat, centerLon,
                                          Config::MAP_W, Config::MAP_H);
  DebugLog::printf("Map view restored: %s center %.5f, %.5f\n",
                   MapRenderer::zoomModeLabel(mapViewport.mode),
                   mapViewport.centerLat, mapViewport.centerLon);
}

void saveMapViewport() {
  if (!mapPreferencesReady) {
    mapViewSavePending = false;
    return;
  }
  mapPreferences.putUChar("mode", static_cast<uint8_t>(mapViewport.mode));
  mapPreferences.putFloat("lat", mapViewport.centerLat);
  mapPreferences.putFloat("lon", mapViewport.centerLon);
  mapViewSavePending = false;
  DebugLog::printf("Map view saved: %s center %.5f, %.5f\n",
                   MapRenderer::zoomModeLabel(mapViewport.mode),
                   mapViewport.centerLat, mapViewport.centerLon);
}

void handleMapTap(int16_t x, int16_t y) {
  float tappedLat = mapViewport.centerLat;
  float tappedLon = mapViewport.centerLon;
  if (!MapRenderer::screenToGeo(mapViewport, x, y, Config::MAP_W,
                                Config::MAP_H, tappedLat, tappedLon)) {
    return;
  }

  const MapZoomMode nextMode = MapRenderer::nextZoomMode(mapViewport.mode);
  mapViewport = MapRenderer::makeViewport(nextMode, tappedLat, tappedLon,
                                          Config::MAP_W, Config::MAP_H);
  radarFrame = 0;
  lastRadarAnimation = millis();
  mapDirty = true;
  mapViewSavePending = true;
  lastMapViewChange = millis();
  DebugLog::printf("Map tap %d,%d -> %s center %.5f, %.5f\n", x, y,
                   MapRenderer::zoomModeLabel(mapViewport.mode),
                   mapViewport.centerLat, mapViewport.centerLon);
}

void printHardwareInfo() {
  Serial.printf("Firmware: %s %s\n", FW_NAME, FW_VERSION);
  Serial.printf("Target: %s\n", FW_TARGET);
  Serial.printf("CPU: %u MHz | cores: %u\n", getCpuFrequencyMhz(),
                static_cast<unsigned>(ESP.getChipCores()));
  Serial.printf("Flash: %u MB | PSRAM: %u MB | free PSRAM: %u kB\n",
                static_cast<unsigned>(ESP.getFlashChipSize() / 1024 / 1024),
                static_cast<unsigned>(ESP.getPsramSize() / 1024 / 1024),
                static_cast<unsigned>(ESP.getFreePsram() / 1024));
  Serial.printf("Heap free: %u kB | reset reason: %d\n",
                static_cast<unsigned>(ESP.getFreeHeap() / 1024),
                static_cast<int>(esp_reset_reason()));
}

bool validateDisplay() {
  lv_disp_t* display = lv_disp_get_default();
  if (!display) {
    Serial.println("Fatal: LVGL display was not registered.");
    return false;
  }
  const lv_coord_t width = lv_disp_get_hor_res(display);
  const lv_coord_t height = lv_disp_get_ver_res(display);
  Serial.printf("LCD detected by LVGL: %d x %d\n", width, height);
  if (width != Config::SCREEN_W || height != Config::SCREEN_H) {
    Serial.printf("Fatal: expected %u x %u. This firmware is only for the original 7-inch model.\n",
                  Config::SCREEN_W, Config::SCREEN_H);
    return false;
  }
  return true;
}

bool due(uint32_t now, uint32_t previous, uint32_t interval) {
  return static_cast<int32_t>(now - previous) >= static_cast<int32_t>(interval);
}

bool networkActivityStale(uint32_t now, uint32_t lastSuccess,
                          uint32_t staleMs) {
  // Allow the system one full stale window after boot before treating a source
  // that has never succeeded as evidence of a wedged network stack.
  if (now < staleMs) return false;
  return lastSuccess == 0U || due(now, lastSuccess, staleMs);
}

void scheduleDisplayRecoveryAfterLoad(uint32_t loopDurationMs) {
  if (loopDurationMs > longestLoopDurationMs) {
    longestLoopDurationMs = loopDurationMs;
  }

  if (!backlightOn || displayResyncPending) return;
  if (loopDurationMs < Config::DISPLAY_LOAD_GUARD_THRESHOLD_MS) return;

  const uint32_t now = millis();
  if (lastDisplaySyncRecovery != 0 &&
      !due(now, lastDisplaySyncRecovery, Config::DISPLAY_LOAD_GUARD_COOLDOWN_MS)) {
    return;
  }

  displayResyncPending = true;
  ++lcdLoadGuardTriggerCount;
  DebugLog::printf(
      "LCD load guard: loop blocked %u ms -> deferred RGB DMA resync\n",
      static_cast<unsigned>(loopDurationMs));
}

void scheduleDisplayRecoveryAfterNetwork(const NetworkWorker::Diagnostics& diag) {
  if (diag.completedJobs == lastNetworkCompletedSeen) return;

  const bool newFailure = diag.failedJobs != lastNetworkFailedSeen;
  lastNetworkCompletedSeen = diag.completedJobs;
  lastNetworkFailedSeen = diag.failedJobs;

  if (!backlightOn || displayResyncPending) return;
  if (!newFailure &&
      diag.lastJobDurationMs < Config::NETWORK_LCD_RECOVERY_THRESHOLD_MS) {
    return;
  }

  const uint32_t now = millis();
  if (lastDisplaySyncRecovery != 0 &&
      !due(now, lastDisplaySyncRecovery, Config::DISPLAY_LOAD_GUARD_COOLDOWN_MS)) {
    return;
  }

  displayResyncPending = true;
  ++lcdLoadGuardTriggerCount;
  DebugLog::printf(
      "LCD network guard: %s job %u ms -> deferred RGB DMA resync\n",
      newFailure ? "failed/slow" : "slow",
      static_cast<unsigned>(diag.lastJobDurationMs));
}

void requestDisplaySyncRecovery(const char* reason) {
  lv_disp_t* display = lv_disp_get_default();
  if (!display || !display->driver || !display->driver->user_data) return;

  auto* lcd = static_cast<ESP_PanelLcd*>(display->driver->user_data);
  if (!lcd || !lcd->getHandle()) return;

  const esp_err_t err = esp_lcd_rgb_panel_restart(lcd->getHandle());
  if (err == ESP_OK) {
    ++lcdResyncCount;
    DebugLog::printf("LCD DMA resync requested: %s\n",
                     reason ? reason : "runtime operation");
  } else {
    DebugLog::printf("LCD DMA resync failed (%d): %s\n",
                     static_cast<int>(err),
                     reason ? reason : "runtime operation");
  }
  lastDisplaySyncRecovery = millis();
}

void setBacklight(bool on, const char* reason);

// During ESP32-S3 flash erase/write the RGB panel can temporarily lose DMA
// alignment because the framebuffer lives in PSRAM. Do not try to repair the
// panel while Update.write() is active: that can stall the synchronous HTTP
// upload. Instead hide the transient artefacts by switching only the physical
// backlight, without touching LVGL. The next boot reinitializes the panel.
void setOtaBacklightRaw(bool on) {
  if (backlightOn == on) return;
  toggle_backlight(lcdBacklightState);
  backlightOn = on;
  DebugLog::printf("OTA display: backlight %s during flash transfer\n",
                   on ? "ON" : "OFF");
}

void forceDisplayRefresh() {
  if (!lvgl_port_lock(500)) return;
  lv_disp_t* display = lv_disp_get_default();
  if (display) lv_refr_now(display);
  lvgl_port_unlock();
}

void handleOtaDisplayEvent(OtaDisplayEvent event, const char* filename,
                           uint32_t bytesWritten, int errorCode) {
  switch (event) {
    case OtaDisplayEvent::Start:
      networkWorker.setPaused(true);
      otaScreenActive = true;
      // If the browser never starts the actual upload, return to the dashboard
      // automatically. UPLOAD_FILE_START cancels this timeout via Progress(0).
      otaScreenDismissAt = millis() + 15000UL;
      setBacklight(true, "OTA update");
      if (lvgl_port_lock(500)) {
        UI::showOtaScreen(filename, FW_VERSION);
        // Do NOT call lv_refr_now() here. On this RGB panel it can wait for a
        // flush/DMA completion and stall the WebServer before the first upload
        // chunk arrives. The normal LVGL task renders the prepared screen.
        lvgl_port_unlock();
      }
      DebugLog::println("OTA display: preflight screen scheduled");
      break;

    case OtaDisplayEvent::Progress:
      // Flash transfer has started. RGB DMA can visibly jump/shift while the
      // ESP32-S3 writes the OTA partition in external flash. Keep every LVGL
      // and panel-DMA call out of this callback and simply blank the physical
      // backlight until reboot. The preflight screen was visible beforehand.
      otaScreenDismissAt = 0;
      setOtaBacklightRaw(false);
      break;

    case OtaDisplayEvent::Success:
      // Success is intentionally display-free. Update.end() arms the reboot
      // before the POST result handler, so touching LVGL here would only add a
      // new opportunity to block the reboot.
      otaScreenActive = true;
      otaScreenDismissAt = 0;
      break;

    case OtaDisplayEvent::Failure:
      networkWorker.setPaused(false);
      // Failure is delivered after WebServer::handleClient() returns. Restore
      // the backlight first, then repair/repaint the RGB panel once flash writes
      // are no longer active.
      setOtaBacklightRaw(true);
      requestDisplaySyncRecovery("OTA failure");
      if (lvgl_port_lock(500)) {
        UI::finishOtaScreen(false, bytesWritten, errorCode);
        lv_disp_t* display = lv_disp_get_default();
        if (display) lv_refr_now(display);
        lvgl_port_unlock();
      }
      otaScreenActive = true;
      otaScreenDismissAt = millis() + 2500U;
      break;
  }
}

bool deadlinePending(uint32_t now, uint32_t deadline) {
  return deadline != 0 &&
         static_cast<int32_t>(deadline - now) > 0;
}

bool dayScheduleActive(const DeviceSettings& settings,
                       const struct tm& localTime) {
  if (!settings.backlightScheduleEnabled) return true;

  // tm_wday: Sunday=0. Configuration array: Monday=0 ... Sunday=6.
  const uint8_t today = static_cast<uint8_t>((localTime.tm_wday + 6) % 7);
  const uint8_t previous = static_cast<uint8_t>((today + 6) % 7);
  const uint16_t minuteOfDay = static_cast<uint16_t>(
      localTime.tm_hour * 60 + localTime.tm_min);

  const BacklightDaySchedule& current = settings.backlightDays[today];
  if (current.enabled) {
    if (current.startMinutes == current.endMinutes) return true;
    if (current.startMinutes < current.endMinutes) {
      if (minuteOfDay >= current.startMinutes &&
          minuteOfDay < current.endMinutes) {
        return true;
      }
    } else if (minuteOfDay >= current.startMinutes) {
      // Interval starts today and continues after midnight.
      return true;
    }
  }

  // Complete an overnight interval started on the previous day.
  const BacklightDaySchedule& previousDay =
      settings.backlightDays[previous];
  return previousDay.enabled &&
         previousDay.startMinutes > previousDay.endMinutes &&
         minuteOfDay < previousDay.endMinutes;
}

void setBacklight(bool on, const char* reason) {
  if (backlightOn == on) return;

  if (!on) {
    lvgl_port_lock(-1);
    UI::setBacklightWakeOverlay(true);
    lvgl_port_unlock();
    toggle_backlight(lcdBacklightState);
  } else {
    toggle_backlight(lcdBacklightState);
    lvgl_port_lock(-1);
    UI::setBacklightWakeOverlay(false);
    lvgl_port_unlock();
  }

  backlightOn = on;
  if (on) {
    // Data collection continues while dark. Redraw only once when the panel is
    // visible again, which reduces unnecessary PSRAM traffic overnight.
    mapDirty = true;
    lastHeaderUpdate = 0;
  }
  DebugLog::printf("Backlight: %s (%s)\n", on ? "ON" : "OFF",
                   reason ? reason : "schedule");
}

void updateBacklightControl(uint32_t now) {
  if (UI::consumeBacklightWakeRequest()) {
    backlightWakeUntil = now + 60000U;
    DebugLog::println("Backlight: touch wake for 60 seconds");
  }

  const DeviceSettings& settings = deviceConfig.settings();
  const time_t epoch = time(nullptr);
  const bool timeReady = epoch > 1700000000;
  bool scheduleWindow = true;
  if (settings.backlightScheduleEnabled && timeReady) {
    struct tm localTime {};
    localtime_r(&epoch, &localTime);
    scheduleWindow = dayScheduleActive(settings, localTime);
  }

  // Until NTP is available the display stays on. This prevents an incorrect
  // clock at boot from unexpectedly switching the backlight off.
  if (!timeReady) scheduleWindow = true;
  if (!settings.backlightScheduleEnabled) scheduleWindow = true;

  if (scheduleWindow) backlightWakeUntil = 0;
  backlightScheduledWindowActive = scheduleWindow;
  backlightTemporaryWake =
      !scheduleWindow && deadlinePending(now, backlightWakeUntil);
  const bool desiredOn = scheduleWindow || backlightTemporaryWake;
  setBacklight(desiredOn, backlightTemporaryWake ? "touch wake" :
                                  (scheduleWindow ? "active schedule" :
                                                    "inactive schedule"));
}

void updateRuntimeDiagnostics() {
  const uint32_t now = millis();
  const AircraftSnapshot& aircraft = adsb.snapshot();
  const WeatherSnapshot& weatherData = weather.snapshot();
  const AstronomySnapshot& astronomyData = astronomy.snapshot();
  const BarometerSnapshot& barometerData = barometer.snapshot();

  runtimeDiagnostics.uptimeMs = now;
  runtimeDiagnostics.lastAdsbUpdateMs = adsb.lastSuccessMs();
  runtimeDiagnostics.lastRadarUpdateMs = lastRadarUpdate;
  runtimeDiagnostics.lastLightningUpdateMs = lightning.lastSuccessMs();
  runtimeDiagnostics.lastCurrentWeatherUpdateMs = lastCurrentWeatherUpdate;
  runtimeDiagnostics.lastForecastUpdateMs = lastForecastUpdate;
  runtimeDiagnostics.lastAstronomyUpdateMs = lastAstronomyUpdate;
  runtimeDiagnostics.lastBarometerUpdateMs = lastBarometerUpdate;
  runtimeDiagnostics.lastDisplaySyncRecoveryMs = lastDisplaySyncRecovery;
  runtimeDiagnostics.lcdResyncCount = lcdResyncCount;
  runtimeDiagnostics.lcdLoadGuardTriggerCount = lcdLoadGuardTriggerCount;
  runtimeDiagnostics.longestLoopDurationMs = longestLoopDurationMs;
  runtimeDiagnostics.mapRedrawCount = mapRedrawCount;
  runtimeDiagnostics.lastMapRedrawDurationMs = lastMapRedrawDurationMs;
  const NetworkWorker::Diagnostics networkDiag = networkWorker.diagnostics();
  runtimeDiagnostics.networkWorkerRunning = networkDiag.running;
  runtimeDiagnostics.networkWorkerPaused = networkDiag.paused;
  runtimeDiagnostics.networkPendingJobs = networkDiag.pendingJobs;
  runtimeDiagnostics.networkLastJobDurationMs = networkDiag.lastJobDurationMs;
  runtimeDiagnostics.networkLongestJobDurationMs = networkDiag.longestJobDurationMs;
  runtimeDiagnostics.networkCompletedJobs = networkDiag.completedJobs;
  runtimeDiagnostics.networkFailedJobs = networkDiag.failedJobs;
  runtimeDiagnostics.networkBackoffSkips = networkDiag.backoffSkips;
  strlcpy(runtimeDiagnostics.networkActiveJob, networkDiag.activeJob,
          sizeof(runtimeDiagnostics.networkActiveJob));
  strlcpy(runtimeDiagnostics.networkLastResult, networkDiag.lastResult,
          sizeof(runtimeDiagnostics.networkLastResult));
  runtimeDiagnostics.radarFrameCount = radar.frameCount();
  runtimeDiagnostics.currentRadarFrame = radarFrame;
  runtimeDiagnostics.forecastSlotCount = weatherData.forecastSlotCount;
  runtimeDiagnostics.aircraftCount = aircraft.count;
  runtimeDiagnostics.localAircraftCount = aircraft.localCount;
  runtimeDiagnostics.adsbFiAircraftCount = aircraft.adsbFiCount;
  runtimeDiagnostics.mlatAircraftCount = aircraft.mlatCount;
  runtimeDiagnostics.radarCacheReady = radar.animationCacheReady();
  runtimeDiagnostics.lightningReady = lightning.ready();
  runtimeDiagnostics.lightningStrikeCount = lightning.strikeCount();
  runtimeDiagnostics.currentWeatherValid = weatherData.current.valid;
  runtimeDiagnostics.weatherPressureHpa =
      weatherData.current.valid ? weatherData.current.pressureHpa : NAN;
  runtimeDiagnostics.forecastValid = weatherData.forecastValid;
  runtimeDiagnostics.astronomyValid = astronomyData.valid;
  runtimeDiagnostics.barometerEnabled = barometerData.enabled;
  runtimeDiagnostics.barometerDetected = barometerData.detected;
  runtimeDiagnostics.barometerValid = barometerData.valid;
  runtimeDiagnostics.barometerAddress = barometerData.i2cAddress;
  runtimeDiagnostics.barometerPressureHpa = barometerData.pressureHpa;
  runtimeDiagnostics.barometerRawPressureHpa = barometerData.rawPressureHpa;
  runtimeDiagnostics.barometerTemperatureC = barometerData.temperatureC;
  runtimeDiagnostics.barometerReductionTemperatureC =
      barometerData.reductionTemperatureC;
  runtimeDiagnostics.wuTemperatureAverageC =
      barometerData.wuTemperatureAverageC;
  runtimeDiagnostics.wuTemperatureSampleCount =
      barometerData.wuTemperatureSampleCount;
  runtimeDiagnostics.wuTemperatureSpanHours =
      barometerData.wuTemperatureSpanHours;
  runtimeDiagnostics.wuTemperatureLatestEpoch =
      barometerData.wuTemperatureLatestEpoch;
  strlcpy(runtimeDiagnostics.barometerReductionTemperatureSource,
          barometerData.reductionTemperatureSource,
          sizeof(runtimeDiagnostics.barometerReductionTemperatureSource));
  runtimeDiagnostics.barometerDelta3hHpa = barometerData.delta3hHpa;
  runtimeDiagnostics.barometerTrendHpaPerHour = barometerData.trendHpaPerHour;
  runtimeDiagnostics.pressureHistoryCount = barometerData.historyCount;
  runtimeDiagnostics.zambrettiReady = barometerData.zambrettiReady;
  runtimeDiagnostics.zambrettiWindUsed = barometerData.zambrettiWindUsed;
  runtimeDiagnostics.zambrettiSeasonApplied =
      barometerData.zambrettiSeasonApplied;
  runtimeDiagnostics.zambrettiAdjustedPressureHpa =
      barometerData.zambrettiAdjustedPressureHpa;
  runtimeDiagnostics.zambrettiWindDirectionDeg =
      barometerData.zambrettiWindDirectionDeg;
  strlcpy(runtimeDiagnostics.zambrettiCode, barometerData.zambrettiCode,
          sizeof(runtimeDiagnostics.zambrettiCode));
  strlcpy(runtimeDiagnostics.zambrettiTrend, barometerData.zambrettiTrend,
          sizeof(runtimeDiagnostics.zambrettiTrend));
  strlcpy(runtimeDiagnostics.barometerSensor, barometerData.sensorName,
          sizeof(runtimeDiagnostics.barometerSensor));
  strlcpy(runtimeDiagnostics.barometerTrend, barometerData.trendText,
          sizeof(runtimeDiagnostics.barometerTrend));
  strlcpy(runtimeDiagnostics.barometerForecast, barometerData.forecastText,
          sizeof(runtimeDiagnostics.barometerForecast));
  strlcpy(runtimeDiagnostics.barometerStatus, barometerData.status,
          sizeof(runtimeDiagnostics.barometerStatus));
  runtimeDiagnostics.backlightOn = backlightOn;
  runtimeDiagnostics.backlightScheduleEnabled =
      deviceConfig.settings().backlightScheduleEnabled;
  runtimeDiagnostics.backlightScheduledWindowActive =
      backlightScheduledWindowActive;
  runtimeDiagnostics.backlightTemporaryWake = backlightTemporaryWake;
  runtimeDiagnostics.backlightWakeRemainingMs =
      backlightTemporaryWake && deadlinePending(now, backlightWakeUntil)
          ? static_cast<uint32_t>(backlightWakeUntil - now)
          : 0;
  strlcpy(runtimeDiagnostics.radarStatus, radar.status(),
          sizeof(runtimeDiagnostics.radarStatus));
  strlcpy(runtimeDiagnostics.lightningStatus, lightning.status(),
          sizeof(runtimeDiagnostics.lightningStatus));
  strlcpy(runtimeDiagnostics.adsbStatus, aircraft.status,
          sizeof(runtimeDiagnostics.adsbStatus));
  strlcpy(runtimeDiagnostics.weatherStatus, weatherData.status,
          sizeof(runtimeDiagnostics.weatherStatus));
  strlcpy(runtimeDiagnostics.astronomyStatus, astronomyData.status,
          sizeof(runtimeDiagnostics.astronomyStatus));
  strlcpy(runtimeDiagnostics.forecastProduct, weatherData.forecastProduct,
          sizeof(runtimeDiagnostics.forecastProduct));
  strlcpy(runtimeDiagnostics.mapView,
          MapRenderer::zoomModeLabel(mapViewport.mode),
          sizeof(runtimeDiagnostics.mapView));
  runtimeDiagnostics.mapZoomMode =
      static_cast<uint8_t>(mapViewport.mode);

  const time_t epoch = time(nullptr);
  runtimeDiagnostics.timeSynchronized = epoch > 1700000000;
  if (runtimeDiagnostics.timeSynchronized) {
    struct tm localTime {};
    localtime_r(&epoch, &localTime);
    strftime(runtimeDiagnostics.localTime,
             sizeof(runtimeDiagnostics.localTime), "%H:%M:%S", &localTime);
    strftime(runtimeDiagnostics.localDate,
             sizeof(runtimeDiagnostics.localDate), "%d.%m.%Y", &localTime);
    strftime(runtimeDiagnostics.timezone,
             sizeof(runtimeDiagnostics.timezone), "%Z", &localTime);
  } else {
    strlcpy(runtimeDiagnostics.localTime, "--:--:--",
            sizeof(runtimeDiagnostics.localTime));
    strlcpy(runtimeDiagnostics.localDate, "--.--.----",
            sizeof(runtimeDiagnostics.localDate));
    strlcpy(runtimeDiagnostics.timezone, "CET/CEST",
            sizeof(runtimeDiagnostics.timezone));
  }
}

void updateHeader() {
  const String network = deviceConfig.networkLabel();
  lvgl_port_lock(-1);
  UI::updateHeader(network.c_str(), radar.status(), adsb.snapshot());
  lvgl_port_unlock();
}

void updateWeatherUi() {
  lvgl_port_lock(-1);
  UI::updateWeather(weather.snapshot());
  lvgl_port_unlock();
}

void updatePressureUi() {
  lvgl_port_lock(-1);
  UI::updatePressure(barometer.snapshot());
  lvgl_port_unlock();
}

void updateAstronomyUi() {
  lvgl_port_lock(-1);
  UI::updateAstronomy(astronomy.snapshot());
  lvgl_port_unlock();
}

void updateAstronomy() {
  const WeatherSnapshot& weatherData = weather.snapshot();
  astronomy.update(weatherData.stationLat, weatherData.stationLon);
  updateAstronomyUi();
  lastAstronomyUpdate = millis();
}

void prepareRadarAnimation() {
  if (radar.frameCount() == 0) return;

  Serial.println("Radar: building compact animation cache...");
  if (!radar.prepareAnimationCache(Config::MAP_W, Config::MAP_H)) {
    Serial.printf("Radar cache unavailable; radar layer disabled: %s\n",
                  radar.status());
  }

  // Allow the RGB DMA task to refill its bounce buffer after the one-time PNG
  // conversion. The panel clock is never changed; a later recovery may only
  // schedule an RGB DMA restart on the next VSYNC.
  delay(40);
}

void redrawMap() {
  const uint32_t redrawStarted = millis();
  lv_obj_t* canvas = nullptr;
  uint16_t* buffer = nullptr;

  // Read the hidden canvas pointers while protected, then prepare its pixels
  // without holding the LVGL mutex. This leaves the LVGL/RGB task free to feed
  // the currently visible framebuffer while the next map is composed.
  lvgl_port_lock(-1);
  canvas = UI::mapCanvas();
  buffer = UI::mapBuffer();
  lvgl_port_unlock();
  if (!canvas || !buffer) return;

  MapRenderer::drawBase(canvas, buffer, Config::MAP_W, Config::MAP_H,
                        mapViewport);

  bool radarRendered = false;
  if (radarLayerEnabled && radar.frameCount() > 0) {
    if (radarFrame >= radar.frameCount()) radarFrame = 0;
    radarRendered = radar.renderFrame(radarFrame, buffer, Config::MAP_W,
                                      Config::MAP_H, mapViewport);
  }

  // LightningMaps is a realtime layer independent of the CHMI radar image, just
  // like ADS-B. Radar animation may change underneath while the same lightning
  // trail remains positioned by lat/lon and ages against the real clock.
  if (lightningLayerEnabled && lightning.ready()) {
    lightning.renderLive(buffer, Config::MAP_W, Config::MAP_H, mapViewport);
  }

  // The proximity warning is realtime and intentionally independent of the
  // animated radar frame. It stays red while a strike from the last 10 min is
  // inside the real 10 km radius around the home/station position.
  lightningProximityAlertActive =
      lightningLayerEnabled &&
      lightning.recentStrikeWithin(homeLat, homeLon,
                                   Config::LIGHTNING_ALERT_RADIUS_KM,
                                   Config::LIGHTNING_ALERT_MAX_AGE_SEC);

  // LVGL object/text operations and the front/back canvas swap stay protected.
  lvgl_port_lock(-1);
  MapRenderer::drawReference(canvas, buffer, Config::MAP_W,
                             Config::MAP_H, mapViewport,
                             radarLayerEnabled, lightningLayerEnabled,
                             adsbLayerEnabled, lightningProximityAlertActive,
                             homeLat, homeLon);
  if (adsbLayerEnabled) {
    MapRenderer::drawAircraft(canvas, buffer, Config::MAP_W, Config::MAP_H,
                              adsb.snapshot(), mapViewport, aircraftAlert);
  }
  if (radarLayerEnabled && radarRendered) {
    time_t radarFrameTimeUtc = 0;
    radar.frameTimeUtc(radarFrame, radarFrameTimeUtc);
    MapRenderer::drawRadarAge(canvas, buffer, Config::MAP_W, Config::MAP_H,
                              radar.frameName(radarFrame), radarFrameTimeUtc,
                              radarFrame, radar.frameCount(),
                              radar.sourceWidth(), radar.sourceHeight());
  } else if (radarLayerEnabled) {
    MapRenderer::drawRadarMessage(canvas, buffer, Config::MAP_W,
                                  Config::MAP_H, radar.status());
  }
  UI::presentMap();
  lvgl_port_unlock();

  mapDirty = false;
  ++mapRedrawCount;
  lastMapRedrawDurationMs = millis() - redrawStarted;
}

void performInitialUpdates() {
  // Runtime Internet work is deliberately not executed here. Once the main
  // screen is visible, NetworkWorker serializes all DNS/TLS/HTTP jobs on core 0
  // and returns only completed snapshots to this task. This keeps startup and
  // the RGB/LVGL task deterministic even when an external service is down.
  if (!deviceConfig.stationConnected()) {
    startupStatus("Wi-Fi data nejsou dostupna; konfiguracni AP je pripraveno", 88);
  } else {
    startupStatus("Sitova data se nactou na pozadi po startu displeje", 68);
    startupStatus("Pocasi a predpoved: pripravuji background worker", 75);
  }

  if (barometerInitialized) {
    startupStatus("Aktualizuji tlak a Zambretti predpoved...", 81);
    updateBarometerSample(millis());
    lastBarometerUpdate = millis();
    updatePressureUi();
  }
  startupStatus("Pocitam Slunce, Mesic a astronomicke udaje...", 86);
  updateAstronomy();
  startupStatus("ADS-B a sitove sluzby se spusti na pozadi...", 91);
}

}  // namespace

void setup() {
  DebugLog::begin(115200);
  DebugLog::println("\nWaveshare 7in Radar + ADS-B + Weather");
  setenv("TZ", Config::TZ_INFO, 1);
  tzset();
  printHardwareInfo();
  deviceConfig.setOtaDisplayCallback(handleOtaDisplayEvent);
  deviceConfig.load();
  applyDeviceSettings();
  loadMapViewport();

  if (!psramFound() || ESP.getPsramSize() < 7UL * 1024UL * 1024UL) {
    Serial.println("Fatal: 8 MB OPI PSRAM is not available. Check PlatformIO memory_type=qio_opi.");
    while (true) delay(1000);
  }

  // Initialize LittleFS and obtain the initial radar PNG files before the RGB
  // panel starts reading PSRAM. This avoids the heaviest flash/PSRAM traffic
  // during LCD operation. Later five-minute updates use one PNG in PSRAM only.
  radar.begin();
  lightning.begin();
  deviceConfig.begin(&adsb.snapshot(), &runtimeDiagnostics);
  if (deviceConfig.stationConnected()) {
    Serial.println("Preloading CHMI radar files before LCD initialization...");
    radar.updateFrames();
  }
  lastRadarUpdate = millis();
  lastLightningUpdate = lightning.lastSuccessMs();
  lastRadarAnimation = millis();

  lcd_init();
  // lcd_init() leaves the CH422G backlight output high. Keep it on throughout
  // the startup presentation; the weekly schedule is applied after the main
  // screen becomes active.
  lcdBacklightState = 1;
  backlightOn = true;

  lvgl_port_lock(-1);
  startupScreenActive = UI::beginStartup(FW_VERSION);
  lvgl_port_unlock();
  startupStatus("Kontroluji rozliseni a ovladac LCD...", 5);
  if (!validateDisplay()) {
    lvgl_port_lock(-1);
    UI::completeStartup("Chyba LCD: ocekavano 800 x 480", false);
    lvgl_port_unlock();
    while (true) delay(1000);
  }

  startupStatus(deviceConfig.stationConnected()
                    ? "Wi-Fi je pripojena; webove nastaveni je aktivni"
                    : "Spusten konfiguracni pristupovy bod Wi-Fi",
                12);
  startupStatus("Pripravuji grafiku, mapu a obrazove buffery...", 20);
  lvgl_port_lock(-1);
  const bool uiOk = UI::begin();
  lvgl_port_unlock();
  if (!uiOk) {
    lvgl_port_lock(-1);
    UI::completeStartup("Nedostatek pameti pro mapu", false);
    lvgl_port_unlock();
    Serial.println("Fatal: map canvas allocation failed. Check PSRAM settings.");
    while (true) delay(1000);
  }

  // lcd_init() has initialized the shared I2C bus used by touch, CH422G and the
  // external connector. Detect the optional pressure sensor only afterwards.
  startupStatus("Hledam barometr na sbernici I2C...", 38);
  const DeviceSettings& initialSettings = deviceConfig.settings();
  barometerInitialized = true;
  lvgl_port_lock(-1);
  barometer.begin(initialSettings.barometerEnabled,
                   initialSettings.barometerAltitudeM,
                   initialSettings.barometerOffsetHpa);
  lvgl_port_unlock();
  // When Wi-Fi is already connected, wait for the first WU observation before
  // accepting the initial pressure-history point. Offline startup still uses
  // the documented 15 C fallback immediately.
  if (!deviceConfig.stationConnected()) {
    updateBarometerSample(millis());
    lastBarometerUpdate = millis();
  }
  updatePressureUi();

  // The LCD and map buffers now own their final PSRAM blocks. Convert each PNG
  // once to a compact 8-bit overlay; animation never decodes PNG again.
  startupStatus("Pripravuji radarovou animaci v PSRAM...", 48);
  prepareRadarAnimation();
  radar.setDisplayActive(true);
  DebugLog::println("Radar runtime mode: RAM-only, LittleFS writes disabled");
  startupStatus("Skladam zakladni mapu Ceske republiky...", 60);
  updateHeader();
  redrawMap();

  performInitialUpdates();
  lastNetworkConnected = deviceConfig.stationConnected();
  startupStatus("Dokoncuji rozhrani a diagnostiku...", 96);
  updateHeader();
  redrawMap();

  updateRuntimeDiagnostics();
  lvgl_port_lock(-1);
  UI::completeStartup("System je pripraven", true);
  lvgl_port_unlock();
  delay(1200);
  lvgl_port_lock(-1);
  UI::showMainScreen();
  lvgl_port_unlock();
  startupScreenActive = false;
  updateBacklightControl(millis());

  if (networkWorker.begin(&radar)) {
    applyDeviceSettings();
    networkWorker.setPaused(!deviceConfig.stationConnected());
    if (deviceConfig.stationConnected()) networkWorker.requestAll(true);
    const uint32_t networkStart = millis();
    lastAdsbLocalRequest = networkStart;
    lastAdsbInternetRequest = networkStart;
    lastCurrentWeatherRequest = networkStart;
    lastForecastRequest = networkStart;
    lastRadarRequest = networkStart;
  } else {
    DebugLog::println("Network worker unavailable; cached/offline data remain active");
  }

  Serial.printf("Startup complete | heap %u kB | PSRAM %u kB\n",
                static_cast<unsigned>(ESP.getFreeHeap() / 1024),
                static_cast<unsigned>(ESP.getFreePsram() / 1024));
}

void loop() {
  const uint32_t loopStartedMs = millis();
  const uint32_t now = loopStartedMs;
  updateRuntimeDiagnostics();
  deviceConfig.loop();

  // On a failed/aborted OTA keep the result visible briefly, then return to
  // the unchanged dashboard and perform one clean RGB-panel resynchronisation.
  if (otaScreenActive && otaScreenDismissAt != 0 &&
      static_cast<int32_t>(now - otaScreenDismissAt) >= 0) {
    if (lvgl_port_lock(500)) {
      UI::hideOtaScreen();
      lv_disp_t* display = lv_disp_get_default();
      if (display) lv_refr_now(display);
      lvgl_port_unlock();
    }
    otaScreenActive = false;
    otaScreenDismissAt = 0;
    requestDisplaySyncRecovery("OTA failure return to dashboard");
    mapDirty = true;
  }

  // While OTA is active (or its simple result screen is intentionally kept
  // visible), do not run radar, LightningMaps, weather, map redraws or settings
  // writes. WebServer still receives upload chunks because deviceConfig.loop()
  // runs first.
  if (deviceConfig.otaInProgress() || otaScreenActive) {
    delay(1);
    return;
  }

  // LightningMaps has its own background network task. Only pass the current
  // enable/bulk-network state here; no WSS/DNS/TLS code runs in the UI loop.
  lightning.setBulkNetworkBusy(networkWorker.diagnostics().running);
  if (lightning.loop(lightningLayerEnabled && deviceConfig.stationConnected())) {
    mapDirty = true;
  }

  // Redraw also when the 10 km proximity warning expires. Without this edge
  // check a paused radar could otherwise leave the red circle visible after
  // the ten-minute warning window elapsed.
  const bool proximityAlertNow =
      lightningLayerEnabled &&
      lightning.recentStrikeWithin(homeLat, homeLon,
                                   Config::LIGHTNING_ALERT_RADIUS_KM,
                                   Config::LIGHTNING_ALERT_MAX_AGE_SEC);
  if (proximityAlertNow != lightningProximityAlertActive) {
    lightningProximityAlertActive = proximityAlertNow;
    mapDirty = true;
    DebugLog::printf("Lightning proximity alert: %s (%.0f km / %u min)\n",
                     proximityAlertNow ? "ACTIVE" : "clear",
                     Config::LIGHTNING_ALERT_RADIUS_KM,
                     static_cast<unsigned>(
                         Config::LIGHTNING_ALERT_MAX_AGE_SEC / 60U));
  }

  if (deviceConfig.consumeLcdResyncRequested()) {
    requestDisplaySyncRecovery("manual web request");
  }

  if (deviceConfig.consumeRuntimeSettingsChanged()) {
    const bool previousRadarLayer = radarLayerEnabled;
    const bool previousLightningLayer = lightningLayerEnabled;
    const bool previousAdsbLayer = adsbLayerEnabled;
    const float previousHomeLat = homeLat;
    const float previousHomeLon = homeLon;
    const AircraftAlertConfig previousAlert = aircraftAlert;
    applyDeviceSettings();
    if (deviceConfig.stationConnected()) {
      networkWorker.request(NetworkWorker::Job::AdsbLocal, true);
      networkWorker.request(NetworkWorker::Job::WeatherCurrent, true);
      networkWorker.request(NetworkWorker::Job::Forecast, true);
    }
    const bool homeChanged = fabsf(previousHomeLat - homeLat) > 0.00001f ||
                             fabsf(previousHomeLon - homeLon) > 0.00001f;
    if (homeChanged) {
      lastCurrentWeatherUpdate = 0;
      lastForecastUpdate = 0;
      lastAstronomyUpdate = 0;
    }
    updateBarometerSample(now);
    lastBarometerUpdate = now;
    updatePressureUi();

    bool alertDisplayChanged = previousAlert.enabled != aircraftAlert.enabled;
    for (size_t slot = 0; slot < AIRCRAFT_ALERT_SLOT_COUNT; ++slot) {
      alertDisplayChanged |=
          strcasecmp(previousAlert.targets[slot], aircraftAlert.targets[slot]) != 0;
    }
    if (previousRadarLayer != radarLayerEnabled ||
        previousLightningLayer != lightningLayerEnabled ||
        previousAdsbLayer != adsbLayerEnabled || alertDisplayChanged ||
        homeChanged) {
      mapDirty = true;
    }

    // Saving web settings writes NVS while the RGB panel is active. Schedule
    // one recovery on the next VSYNC after the new settings are applied.
    displayResyncPending = true;
    DebugLog::println("Runtime web settings applied without restart");
  }

  MapZoomMode requestedMapZoom = MapZoomMode::Full;
  if (deviceConfig.consumeMapZoomRequested(requestedMapZoom)) {
    mapViewport = MapRenderer::makeViewport(
        requestedMapZoom, homeLat, homeLon, Config::MAP_W, Config::MAP_H);
    radarFrame = 0;
    lastRadarAnimation = now;
    mapDirty = true;
    // Web settings may also change Wi-Fi and schedule a reboot shortly after
    // saving. Persist this explicit web map choice immediately so it survives
    // that reboot instead of waiting for the delayed touch-save timer.
    saveMapViewport();
    displayResyncPending = true;
    DebugLog::printf("Map web: %s centered on HOME %.5f, %.5f\n",
                     MapRenderer::zoomModeLabel(mapViewport.mode),
                     homeLat, homeLon);
  }

  updateBacklightControl(now);

  // Runtime Wi-Fi recovery is a non-blocking state machine. It never waits in
  // a delay()/poll loop for an SSID, so a missing router cannot stall LVGL/RGB.
  deviceConfig.serviceNetwork();
  const bool networkConnected = deviceConfig.stationConnected();
  if (networkConnected != lastNetworkConnected) {
    if (networkConnected) {
      DebugLog::println("WiFi restored: queueing network refresh without blocking UI");
      networkWorker.setPaused(false);
      applyDeviceSettings();
      networkWorker.requestAll(true);
      const uint32_t requested = millis();
      lastAdsbLocalRequest = requested;
      lastAdsbInternetRequest = requested;
      lastRadarRequest = requested;
      lastCurrentWeatherRequest = requested;
      lastForecastRequest = requested;
    } else {
      DebugLog::println("WiFi lost: network worker paused; cached data remain visible");
      networkWorker.setPaused(true);
    }
    lastNetworkConnected = networkConnected;
  }

  // Import only completed background results. These operations are short
  // cache copies/swaps in the main task; no socket/TLS/JSON work happens here.
  bool aircraftChanged = false;
  aircraftChanged |= networkWorker.consumeLocalAdsb(adsb);
  aircraftChanged |= networkWorker.consumeInternetAdsb(adsb);
  if (aircraftChanged) {
    lastAdsbUpdate = adsb.lastSuccessMs();
    mapDirty = true;
  }

  if (networkWorker.consumeCurrentWeather(weather)) {
    lastCurrentWeatherUpdate = now;
    updateWeatherUi();
    if (barometerInitialized) {
      updateBarometerSample(now);
      lastBarometerUpdate = now;
      if (backlightOn) updatePressureUi();
    }
  }

  if (networkWorker.consumeForecast(weather)) {
    lastForecastUpdate = now;
    updateWeatherUi();
  }

  if (networkWorker.consumeRadar(radar)) {
    lastRadarUpdate = now;
    radarFrame = 0;
    lastRadarAnimation = now;
    mapDirty = true;
    // The cache swap itself is short, but one deferred VSYNC resync keeps the
    // same safety net used after other large PSRAM operations.
    displayResyncPending = true;
  }

  // Age out stale ADS-B caches even when a provider is down. No network access
  // is performed by refreshMergedSnapshot().
  if (due(now, lastAdsbMerge, 1000UL)) {
    lastAdsbMerge = now;
    adsb.refreshMergedSnapshot();
  }

  // Background networking no longer blocks loop(), so use worker completion
  // statistics to detect exceptional TLS/PSRAM load that may still disturb the
  // RGB DMA. Recovery remains cooldown-limited, never periodic.
  scheduleDisplayRecoveryAfterNetwork(networkWorker.diagnostics());

  // Last-resort Wi-Fi/network-stack recovery. WL_CONNECTED can remain true
  // even when DNS/TCP/TLS sockets are wedged. Require several independent
  // feeds to be stale before rebuilding Wi-Fi, so a single provider outage
  // never triggers this path. The configuration AP is exposed immediately.
  if (networkConnected && adsbLayerEnabled && lightningLayerEnabled) {
    const DeviceSettings& settings = deviceConfig.settings();
    const bool internetAdsbStale = networkActivityStale(
        now, adsb.lastNetworkSuccessMs(), Config::NETWORK_GLOBAL_STALE_MS);
    const bool lightningStale = networkActivityStale(
        now, lightning.lastSuccessMs(), Config::NETWORK_GLOBAL_STALE_MS);
    const bool localAdsbStale =
        !settings.localAdsbEnabled || settings.adsbUrl.isEmpty() ||
        networkActivityStale(now, adsb.lastLocalSuccessMs(),
                             Config::NETWORK_GLOBAL_STALE_MS);
    const bool recoveryAllowed =
        lastGlobalNetworkRecovery == 0U ||
        due(now, lastGlobalNetworkRecovery,
            Config::NETWORK_RECOVERY_COOLDOWN_MS);

    if (internetAdsbStale && lightningStale && localAdsbStale &&
        recoveryAllowed) {
      DebugLog::println(
          "Network health watchdog: ADS-B + LightningMaps stale -> Wi-Fi rebuild + recovery AP");
      lastGlobalNetworkRecovery = now;
      networkWorker.setPaused(true);
      deviceConfig.forceNetworkRecovery("network feeds stale - recovery AP");
      lastNetworkConnected = false;
      delay(1);
      return;
    }
  }

  int16_t mapTapX = 0;
  int16_t mapTapY = 0;
  if (UI::consumeMapTap(mapTapX, mapTapY)) {
    handleMapTap(mapTapX, mapTapY);
  }

  const bool manualRefresh = UI::consumeManualRefresh();
  if (manualRefresh && networkConnected) {
    DebugLog::println("Manual refresh requested -> background queue");
    networkWorker.requestAll(true);
    lastAdsbLocalRequest = lastAdsbInternetRequest = now;
    lastRadarRequest = lastCurrentWeatherRequest = lastForecastRequest = now;
  }

  // All runtime DNS/TCP/TLS/HTTP jobs below are only queued. NetworkWorker
  // executes one at a time on core 0, preventing simultaneous TLS clients from
  // exhausting internal heap or starving the RGB/LVGL task.
  if (networkConnected && deviceConfig.settings().localAdsbEnabled &&
      due(now, lastAdsbLocalRequest, Config::ADSB_REFRESH_MS)) {
    lastAdsbLocalRequest = now;
    networkWorker.request(NetworkWorker::Job::AdsbLocal);
  }

  if (networkConnected &&
      due(now, lastAdsbInternetRequest, Config::ADSB_FI_REFRESH_MS)) {
    lastAdsbInternetRequest = now;
    networkWorker.request(NetworkWorker::Job::AdsbInternet);
  }

  if (networkConnected &&
      due(now, lastRadarRequest, Config::RADAR_REFRESH_MS)) {
    lastRadarRequest = now;
    networkWorker.request(NetworkWorker::Job::Radar);
  }

  if (networkConnected &&
      due(now, lastCurrentWeatherRequest,
          Config::CURRENT_WEATHER_REFRESH_MS)) {
    lastCurrentWeatherRequest = now;
    networkWorker.request(NetworkWorker::Job::WeatherCurrent);
  }

  if (networkConnected &&
      due(now, lastForecastRequest, Config::FORECAST_REFRESH_MS)) {
    lastForecastRequest = now;
    networkWorker.request(NetworkWorker::Job::Forecast);
  }

  if (due(now, lastBarometerUpdate, Config::BAROMETER_REFRESH_MS)) {
    lastBarometerUpdate = now;
    updateBarometerSample(now);
    if (backlightOn) updatePressureUi();
  }

  if (due(now, lastAstronomyUpdate, Config::ASTRONOMY_REFRESH_MS)) {
    updateAstronomy();
  }

  if (backlightOn && radarLayerEnabled && !UI::radarPaused() &&
      radar.frameCount() > 1 &&
      due(now, lastRadarAnimation, Config::RADAR_ANIMATION_MS)) {
    lastRadarAnimation = now;
    radarFrame = (radarFrame + 1) % radar.frameCount();
    mapDirty = true;
  }

  if (mapDirty && backlightOn) redrawMap();

  if (displayResyncPending) {
    delay(25);
    requestDisplaySyncRecovery("flash/RAM operation complete");
    displayResyncPending = false;
  }

  if (backlightOn && due(now, lastHeaderUpdate, 1000)) {
    lastHeaderUpdate = now;
    updateHeader();
  }

  // One delayed NVS write stores only the final selection after repeated taps.
  // This minimizes flash traffic while the RGB panel is active.
  if (mapViewSavePending && due(now, lastMapViewChange, 1500)) {
    saveMapViewport();
    delay(20);
    displayResyncPending = true;
  }

  // Periodic output remains visible even when the serial monitor is opened
  // after boot. It is written to USB CDC and hardware UART0.
  if (due(now, lastDebugHeartbeat, 10000)) {
    lastDebugHeartbeat = now;
    const WeatherSnapshot& ws = weather.snapshot();
    DebugLog::printf(
        "HEARTBEAT ms=%u WiFi=%d AP=%d heap=%u kB forecast=%s cards=%u layers=R%d/L%d/A%d alerts=%s [%s|%s|%s] BL=%d schedule=%d wake=%d baro=%s %.1f hPa d3h=%+.1f Z=%s\n",
        static_cast<unsigned>(now),
        deviceConfig.stationConnected() ? 1 : 0,
        deviceConfig.portalActive() ? 1 : 0,
        static_cast<unsigned>(ESP.getFreeHeap() / 1024),
        ws.forecastValid ? ws.forecastProduct : "FAILED",
        static_cast<unsigned>(ws.forecastSlotCount),
        radarLayerEnabled ? 1 : 0, lightningLayerEnabled ? 1 : 0,
        adsbLayerEnabled ? 1 : 0,
        aircraftAlert.enabled ? "on" : "off", aircraftAlert.targets[0],
        aircraftAlert.targets[1], aircraftAlert.targets[2],
        backlightOn ? 1 : 0, backlightScheduledWindowActive ? 1 : 0,
        backlightTemporaryWake ? 1 : 0,
        barometer.snapshot().sensorName,
        barometer.snapshot().valid ? barometer.snapshot().pressureHpa : NAN,
        barometer.snapshot().delta3hHpa,
        barometer.snapshot().zambrettiCode);
  }

  // Detect only genuinely long blocking iterations. This is deliberately not
  // a timer-based panel restart: it reacts to heavy network/PSRAM work and
  // then uses the same proven deferred recovery as the manual web button.
  const uint32_t loopDurationMs = millis() - loopStartedMs;
  scheduleDisplayRecoveryAfterLoad(loopDurationMs);

  delay(5);
}
