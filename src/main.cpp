#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Waveshare_ST7262_LVGL.h>
#include <esp_system.h>

#include "adsb_service.h"
#include "astronomy_service.h"
#include "config.h"
#include "debug_log.h"
#include "map_renderer.h"
#include "radar_service.h"
#include "secrets.h"
#include "ui.h"
#include "weather_service.h"
#include "version.h"

AdsbService adsb(ADSB_AIRCRAFT_URL);
AstronomyService astronomy;
WeatherService weather(WU_API_KEY, WU_STATION_ID);
RadarService radar;
Preferences mapPreferences;
MapViewport mapViewport;

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
bool mapDirty = true;
bool mapViewSavePending = false;
bool mapPreferencesReady = false;
uint32_t lastMapViewChange = 0;

namespace {
bool isUnsetSecret(const char* value) {
  if (!value || !value[0]) return true;
  return strstr(value, "YOUR_") != nullptr || strstr(value, "CHANGE_ME") != nullptr;
}

bool wifiConfigured() {
  return !isUnsetSecret(WIFI_SSID) && !isUnsetSecret(WIFI_PASSWORD);
}

bool weatherConfigured() {
  return !isUnsetSecret(WU_API_KEY) && WU_STATION_ID && WU_STATION_ID[0];
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

void connectWifi(uint32_t timeoutMs = 12000) {
  if (WiFi.status() == WL_CONNECTED) return;
  if (!wifiConfigured()) {
    Serial.println("WiFi is not configured. Edit include/secrets.h.");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < timeoutMs) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected: %s, IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    configTzTime(Config::TZ_INFO, "pool.ntp.org", "time.cloudflare.com");
    struct tm timeInfo {};
    if (getLocalTime(&timeInfo, 5000)) {
      Serial.printf("Local time synchronized: %02d:%02d\n", timeInfo.tm_hour,
                    timeInfo.tm_min);
    } else {
      Serial.println("NTP synchronization will continue in background.");
    }
  } else {
    Serial.println("WiFi connection timed out; the UI will keep retrying.");
  }
}

void updateHeader() {
  lvgl_port_lock(-1);
  UI::updateHeader(WiFi.status() == WL_CONNECTED, radar.status(),
                   adsb.snapshot());
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
  // conversion. The panel timing is never changed or restarted at runtime.
  delay(40);
}

void redrawMap() {
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
  if (radar.frameCount() > 0) {
    if (radarFrame >= radar.frameCount()) radarFrame = 0;
    radarRendered = radar.renderFrame(radarFrame, buffer, Config::MAP_W,
                                      Config::MAP_H, mapViewport);
  }

  // LVGL object/text operations and the front/back canvas swap stay protected.
  lvgl_port_lock(-1);
  MapRenderer::drawReference(canvas, buffer, Config::MAP_W,
                             Config::MAP_H, mapViewport);
  MapRenderer::drawAircraft(canvas, buffer, Config::MAP_W, Config::MAP_H,
                            adsb.snapshot(), mapViewport);
  if (radarRendered) {
    MapRenderer::drawRadarAge(canvas, buffer, Config::MAP_W, Config::MAP_H,
                              radar.frameName(radarFrame), radarFrame,
                              radar.frameCount(), radar.sourceWidth(),
                              radar.sourceHeight());
  } else {
    MapRenderer::drawRadarMessage(canvas, buffer, Config::MAP_W,
                                  Config::MAP_H, radar.status());
  }
  UI::presentMap();
  lvgl_port_unlock();

  mapDirty = false;
}

void performInitialUpdates() {
  if (WiFi.status() != WL_CONNECTED) return;

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
  printHardwareInfo();
  loadMapViewport();

  if (!psramFound() || ESP.getPsramSize() < 7UL * 1024UL * 1024UL) {
    Serial.println("Fatal: 8 MB OPI PSRAM is not available. Check PlatformIO memory_type=qio_opi.");
    while (true) delay(1000);
  }

  // Initialize LittleFS and obtain the initial radar PNG files before the RGB
  // panel starts reading PSRAM. This avoids the heaviest flash/PSRAM traffic
  // during LCD operation. Later five-minute updates are incremental (one PNG).
  radar.begin();
  connectWifi();
  if (WiFi.status() == WL_CONNECTED) {
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

  // The LCD and map buffers now own their final PSRAM blocks. Convert each PNG
  // once to a compact 8-bit overlay; animation never decodes PNG again.
  prepareRadarAnimation();
  updateHeader();
  redrawMap();

  performInitialUpdates();
  updateHeader();
  redrawMap();

  Serial.printf("Startup complete | heap %u kB | PSRAM %u kB\n",
                static_cast<unsigned>(ESP.getFreeHeap() / 1024),
                static_cast<unsigned>(ESP.getFreePsram() / 1024));
}

void loop() {
  const uint32_t now = millis();

  if (WiFi.status() != WL_CONNECTED &&
      due(now, lastWifiRetry, Config::WIFI_RETRY_MS)) {
    lastWifiRetry = now;
    connectWifi(4000);
  }

  int16_t mapTapX = 0;
  int16_t mapTapY = 0;
  if (UI::consumeMapTap(mapTapX, mapTapY)) {
    handleMapTap(mapTapX, mapTapY);
  }

  const bool manualRefresh = UI::consumeManualRefresh();
  if (manualRefresh && WiFi.status() == WL_CONNECTED) {
    DebugLog::println("Manual refresh requested");
    if (weatherConfigured()) weather.updateCurrent();
    weather.updateForecast();
    updateWeatherUi();
    updateAstronomy();
    adsb.update();
    if (radar.updateFrames()) prepareRadarAnimation();
    radarFrame = 0;
    lastCurrentWeatherUpdate = lastForecastUpdate = millis();
    lastAdsbUpdate = lastRadarUpdate = millis();
    lastRadarAnimation = millis();
    mapDirty = true;
  }

  if (WiFi.status() == WL_CONNECTED &&
      due(now, lastAdsbUpdate, Config::ADSB_REFRESH_MS)) {
    lastAdsbUpdate = now;
    adsb.update();
    mapDirty = true;
  }

  if (WiFi.status() == WL_CONNECTED &&
      due(now, lastRadarUpdate, Config::RADAR_REFRESH_MS)) {
    lastRadarUpdate = now;
    if (radar.updateFrames()) {
      prepareRadarAnimation();
      radarFrame = 0;
      lastRadarAnimation = now;
      mapDirty = true;
    }
  }

  if (weatherConfigured() && WiFi.status() == WL_CONNECTED &&
      due(now, lastCurrentWeatherUpdate,
          Config::CURRENT_WEATHER_REFRESH_MS)) {
    lastCurrentWeatherUpdate = now;
    weather.updateCurrent();
    updateWeatherUi();
  }

  if (WiFi.status() == WL_CONNECTED &&
      due(now, lastForecastUpdate, Config::FORECAST_REFRESH_MS)) {
    lastForecastUpdate = now;
    weather.updateForecast();
    updateWeatherUi();
  }

  if (due(now, lastAstronomyUpdate, Config::ASTRONOMY_REFRESH_MS)) {
    updateAstronomy();
  }

  if (!UI::radarPaused() && radar.frameCount() > 1 &&
      due(now, lastRadarAnimation, Config::RADAR_ANIMATION_MS)) {
    lastRadarAnimation = now;
    radarFrame = (radarFrame + 1) % radar.frameCount();
    mapDirty = true;
  }

  if (mapDirty) redrawMap();

  if (due(now, lastHeaderUpdate, 2000)) {
    lastHeaderUpdate = now;
    updateHeader();
  }

  // One delayed NVS write stores only the final selection after repeated taps.
  // This minimizes flash traffic while the RGB panel is active.
  if (mapViewSavePending && due(now, lastMapViewChange, 1500)) {
    saveMapViewport();
    delay(20);
  }

  // Periodic output remains visible even when the serial monitor is opened
  // after boot. It is written to USB CDC and hardware UART0.
  if (due(now, lastDebugHeartbeat, 10000)) {
    lastDebugHeartbeat = now;
    const WeatherSnapshot& ws = weather.snapshot();
    DebugLog::printf("HEARTBEAT ms=%u WiFi=%d heap=%u kB forecast=%s cards=%u\n",
                     static_cast<unsigned>(now),
                     static_cast<int>(WiFi.status()),
                     static_cast<unsigned>(ESP.getFreeHeap() / 1024),
                     ws.forecastValid ? ws.forecastProduct : "FAILED",
                     static_cast<unsigned>(ws.forecastSlotCount));
  }

  delay(5);
}
