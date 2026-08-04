#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Waveshare_ST7262_LVGL.h>
#include <esp_system.h>
#include <esp_lcd_panel_rgb.h>
#include <time.h>

#include "adsb_service.h"
#include "astronomy_service.h"
#include "config.h"
#include "debug_log.h"
#include "device_config.h"
#include "map_renderer.h"
#include "radar_service.h"
#include "ui.h"
#include "weather_service.h"
#include "version.h"

AdsbService adsb("");
AstronomyService astronomy;
WeatherService weather("", "");
RadarService radar;
DeviceConfigService deviceConfig;
AircraftAlertConfig aircraftAlert;
bool radarLayerEnabled = true;
bool adsbLayerEnabled = true;
Preferences mapPreferences;
MapViewport mapViewport;
RuntimeDiagnostics runtimeDiagnostics;

uint8_t radarFrame = 0;
uint32_t lastAdsbUpdate = 0;
uint32_t lastRadarUpdate = 0;
uint32_t lastRadarAnimation = 0;
uint32_t lastCurrentWeatherUpdate = 0;
uint32_t lastForecastUpdate = 0;
uint32_t lastAstronomyUpdate = 0;
uint32_t lastWifiRetry = 0;
uint32_t lastHeaderUpdate = 0;
uint32_t lastDebugHeartbeat = 0;
uint32_t lastDisplaySyncRecovery = 0;
bool mapDirty = true;
bool mapViewSavePending = false;
bool mapPreferencesReady = false;
bool displayResyncPending = false;
bool lastNetworkConnected = false;
uint32_t lastMapViewChange = 0;
uint32_t lcdResyncCount = 0;
uint32_t mapRedrawCount = 0;
uint32_t lastMapRedrawDurationMs = 0;
int lcdBacklightState = 1;
bool backlightOn = true;
bool backlightScheduledWindowActive = true;
bool backlightTemporaryWake = false;
uint32_t backlightWakeUntil = 0;

namespace {
bool isUnsetValue(const String& value) {
  return value.isEmpty() || value.indexOf("YOUR_") >= 0 ||
         value.indexOf("CHANGE_ME") >= 0;
}

bool weatherConfigured() {
  const DeviceSettings& settings = deviceConfig.settings();
  return !isUnsetValue(settings.wuApiKey) &&
         !settings.wuStationId.isEmpty();
}

void applyDeviceSettings() {
  const DeviceSettings& settings = deviceConfig.settings();
  adsb.setAircraftUrl(settings.adsbUrl);
  weather.setConfig(settings.wuApiKey, settings.wuStationId);
  aircraftAlert = deviceConfig.alertConfig();
  radarLayerEnabled = settings.radarLayerEnabled;
  adsbLayerEnabled = settings.adsbLayerEnabled;
  DebugLog::printf(
      "Runtime config: ADSB=%s WU=%s layers=radar:%d adsb:%d alerts=%s [%s|%s|%s] backlightSchedule=%s\n",
      settings.adsbUrl.c_str(), settings.wuStationId.c_str(),
      radarLayerEnabled ? 1 : 0, adsbLayerEnabled ? 1 : 0,
      aircraftAlert.enabled ? "on" : "off", aircraftAlert.targets[0],
      aircraftAlert.targets[1], aircraftAlert.targets[2],
      settings.backlightScheduleEnabled ? "on" : "off");
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
        MapZoomMode::Full, Config::FALLBACK_LAT, Config::FALLBACK_LON,
        Config::MAP_W, Config::MAP_H);
    DebugLog::println("Map view: NVS unavailable, using full Czech Republic");
    return;
  }

  const MapZoomMode mode =
      storedZoomMode(mapPreferences.getUChar("mode", 0));
  const float centerLat =
      mapPreferences.getFloat("lat", Config::FALLBACK_LAT);
  const float centerLon =
      mapPreferences.getFloat("lon", Config::FALLBACK_LON);
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

  runtimeDiagnostics.uptimeMs = now;
  runtimeDiagnostics.lastAdsbUpdateMs = lastAdsbUpdate;
  runtimeDiagnostics.lastRadarUpdateMs = lastRadarUpdate;
  runtimeDiagnostics.lastCurrentWeatherUpdateMs = lastCurrentWeatherUpdate;
  runtimeDiagnostics.lastForecastUpdateMs = lastForecastUpdate;
  runtimeDiagnostics.lastAstronomyUpdateMs = lastAstronomyUpdate;
  runtimeDiagnostics.lastDisplaySyncRecoveryMs = lastDisplaySyncRecovery;
  runtimeDiagnostics.lcdResyncCount = lcdResyncCount;
  runtimeDiagnostics.mapRedrawCount = mapRedrawCount;
  runtimeDiagnostics.lastMapRedrawDurationMs = lastMapRedrawDurationMs;
  runtimeDiagnostics.radarFrameCount = radar.frameCount();
  runtimeDiagnostics.currentRadarFrame = radarFrame;
  runtimeDiagnostics.forecastSlotCount = weatherData.forecastSlotCount;
  runtimeDiagnostics.aircraftCount = aircraft.count;
  runtimeDiagnostics.radarCacheReady = radar.animationCacheReady();
  runtimeDiagnostics.currentWeatherValid = weatherData.current.valid;
  runtimeDiagnostics.forecastValid = weatherData.forecastValid;
  runtimeDiagnostics.astronomyValid = astronomyData.valid;
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

  // LVGL object/text operations and the front/back canvas swap stay protected.
  lvgl_port_lock(-1);
  MapRenderer::drawReference(canvas, buffer, Config::MAP_W,
                             Config::MAP_H, mapViewport,
                             radarLayerEnabled, adsbLayerEnabled);
  if (adsbLayerEnabled) {
    MapRenderer::drawAircraft(canvas, buffer, Config::MAP_W, Config::MAP_H,
                              adsb.snapshot(), mapViewport, aircraftAlert);
  }
  if (radarLayerEnabled && radarRendered) {
    MapRenderer::drawRadarAge(canvas, buffer, Config::MAP_W, Config::MAP_H,
                              radar.frameName(radarFrame), radarFrame,
                              radar.frameCount(), radar.sourceWidth(),
                              radar.sourceHeight());
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
  if (!deviceConfig.stationConnected()) return;

  DebugLog::println("Initial weather update started");
  if (weatherConfigured()) {
    weather.updateCurrent();
  } else {
    DebugLog::println("PWS current skipped: WU key is not configured");
  }
  // Forecast always runs. WU is attempted when a key is present and
  // Open-Meteo remains available without a WU subscription.
  weather.updateForecast();
  updateWeatherUi();
  lastCurrentWeatherUpdate = millis();
  lastForecastUpdate = millis();
  updateAstronomy();

  Serial.println("Loading local ADS-B aircraft.json...");
  adsb.update();
  lastAdsbUpdate = millis();

}
}  // namespace

void setup() {
  DebugLog::begin(115200);
  DebugLog::println("\nWaveshare 7in Radar + ADS-B + Weather");
  setenv("TZ", Config::TZ_INFO, 1);
  tzset();
  printHardwareInfo();
  loadMapViewport();
  deviceConfig.load();
  applyDeviceSettings();

  if (!psramFound() || ESP.getPsramSize() < 7UL * 1024UL * 1024UL) {
    Serial.println("Fatal: 8 MB OPI PSRAM is not available. Check PlatformIO memory_type=qio_opi.");
    while (true) delay(1000);
  }

  // Initialize LittleFS and obtain the initial radar PNG files before the RGB
  // panel starts reading PSRAM. This avoids the heaviest flash/PSRAM traffic
  // during LCD operation. Later five-minute updates use one PNG in PSRAM only.
  radar.begin();
  deviceConfig.begin(&adsb.snapshot(), &runtimeDiagnostics);
  if (deviceConfig.stationConnected()) {
    Serial.println("Preloading CHMI radar files before LCD initialization...");
    radar.updateFrames();
  }
  lastRadarUpdate = millis();
  lastRadarAnimation = millis();

  lcd_init();
  if (!validateDisplay()) {
    while (true) delay(1000);
  }
  lvgl_port_lock(-1);
  const bool uiOk = UI::begin();
  lvgl_port_unlock();
  if (!uiOk) {
    Serial.println("Fatal: map canvas allocation failed. Check PSRAM settings.");
    while (true) delay(1000);
  }
  // lcd_init() leaves the CH422G backlight output high. Keep the software
  // state synchronized before applying the weekly schedule.
  lcdBacklightState = 1;
  backlightOn = true;
  updateBacklightControl(millis());

  // The LCD and map buffers now own their final PSRAM blocks. Convert each PNG
  // once to a compact 8-bit overlay; animation never decodes PNG again.
  prepareRadarAnimation();
  radar.setDisplayActive(true);
  DebugLog::println("Radar runtime mode: RAM-only, LittleFS writes disabled");
  updateHeader();
  redrawMap();

  performInitialUpdates();
  lastNetworkConnected = deviceConfig.stationConnected();
  updateHeader();
  redrawMap();

  updateRuntimeDiagnostics();
  Serial.printf("Startup complete | heap %u kB | PSRAM %u kB\n",
                static_cast<unsigned>(ESP.getFreeHeap() / 1024),
                static_cast<unsigned>(ESP.getFreePsram() / 1024));
}

void loop() {
  const uint32_t now = millis();
  updateRuntimeDiagnostics();
  deviceConfig.loop();

  if (deviceConfig.consumeLcdResyncRequested()) {
    requestDisplaySyncRecovery("manual web request");
  }

  if (deviceConfig.consumeRuntimeSettingsChanged()) {
    const bool previousRadarLayer = radarLayerEnabled;
    const bool previousAdsbLayer = adsbLayerEnabled;
    const AircraftAlertConfig previousAlert = aircraftAlert;
    applyDeviceSettings();

    bool alertDisplayChanged = previousAlert.enabled != aircraftAlert.enabled;
    for (size_t slot = 0; slot < AIRCRAFT_ALERT_SLOT_COUNT; ++slot) {
      alertDisplayChanged |=
          strcasecmp(previousAlert.targets[slot], aircraftAlert.targets[slot]) != 0;
    }
    if (previousRadarLayer != radarLayerEnabled ||
        previousAdsbLayer != adsbLayerEnabled || alertDisplayChanged) {
      mapDirty = true;
    }

    // Saving web settings writes NVS while the RGB panel is active. Schedule
    // one recovery on the next VSYNC after the new settings are applied.
    displayResyncPending = true;
    DebugLog::println("Runtime web settings applied without restart");
  }

  updateBacklightControl(now);

  if (!deviceConfig.stationConnected() &&
      due(now, lastWifiRetry, Config::WIFI_RETRY_MS)) {
    lastWifiRetry = now;
    deviceConfig.ensureNetwork(4000);
  }

  const bool networkConnected = deviceConfig.stationConnected();
  if (networkConnected && !lastNetworkConnected) {
    DebugLog::println("WiFi restored: refreshing network data");
    performInitialUpdates();
    mapDirty = true;
  }
  lastNetworkConnected = networkConnected;

  int16_t mapTapX = 0;
  int16_t mapTapY = 0;
  if (UI::consumeMapTap(mapTapX, mapTapY)) {
    handleMapTap(mapTapX, mapTapY);
  }

  const bool manualRefresh = UI::consumeManualRefresh();
  if (manualRefresh && deviceConfig.stationConnected()) {
    DebugLog::println("Manual refresh requested");
    if (weatherConfigured()) weather.updateCurrent();
    weather.updateForecast();
    updateWeatherUi();
    updateAstronomy();
    adsb.update();
    if (radar.updateFrames()) {
      if (!radar.animationCacheReady()) prepareRadarAnimation();
      displayResyncPending = true;
    }
    radarFrame = 0;
    lastCurrentWeatherUpdate = lastForecastUpdate = millis();
    lastAdsbUpdate = lastRadarUpdate = millis();
    lastRadarAnimation = millis();
    mapDirty = true;
  }

  if (deviceConfig.stationConnected() &&
      due(now, lastAdsbUpdate, Config::ADSB_REFRESH_MS)) {
    lastAdsbUpdate = now;
    adsb.update();
    mapDirty = true;
  }

  if (deviceConfig.stationConnected() &&
      due(now, lastRadarUpdate, Config::RADAR_REFRESH_MS)) {
    lastRadarUpdate = now;
    if (radar.updateFrames()) {
      if (!radar.animationCacheReady()) prepareRadarAnimation();
      radarFrame = 0;
      lastRadarAnimation = now;
      mapDirty = true;
      displayResyncPending = true;
    }
  }

  if (weatherConfigured() && deviceConfig.stationConnected() &&
      due(now, lastCurrentWeatherUpdate,
          Config::CURRENT_WEATHER_REFRESH_MS)) {
    lastCurrentWeatherUpdate = now;
    weather.updateCurrent();
    updateWeatherUi();
  }

  if (deviceConfig.stationConnected() &&
      due(now, lastForecastUpdate, Config::FORECAST_REFRESH_MS)) {
    lastForecastUpdate = now;
    weather.updateForecast();
    updateWeatherUi();
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
        "HEARTBEAT ms=%u WiFi=%d AP=%d heap=%u kB forecast=%s cards=%u layers=R%d/A%d alerts=%s [%s|%s|%s] BL=%d schedule=%d wake=%d\n",
        static_cast<unsigned>(now),
        deviceConfig.stationConnected() ? 1 : 0,
        deviceConfig.portalActive() ? 1 : 0,
        static_cast<unsigned>(ESP.getFreeHeap() / 1024),
        ws.forecastValid ? ws.forecastProduct : "FAILED",
        static_cast<unsigned>(ws.forecastSlotCount),
        radarLayerEnabled ? 1 : 0, adsbLayerEnabled ? 1 : 0,
        aircraftAlert.enabled ? "on" : "off", aircraftAlert.targets[0],
        aircraftAlert.targets[1], aircraftAlert.targets[2],
        backlightOn ? 1 : 0, backlightScheduledWindowActive ? 1 : 0,
        backlightTemporaryWake ? 1 : 0);
  }

  delay(5);
}
