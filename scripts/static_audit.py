#!/usr/bin/env python3
"""Focused static audit for v0.30.0 background network-worker architecture."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


pio = read("platformio.ini")
config = read("include/config.h")
version = read("include/version.h")
models = read("include/models.h")
main = read("src/main.cpp")
device_h = read("src/device_config.h")
device_cpp = read("src/device_config.cpp")
barometer_h = read("src/barometer_service.h")
barometer_cpp = read("src/barometer_service.cpp")
bmp180_h = read("src/bmp180_shared_i2c.h")
bmp180_cpp = read("src/bmp180_shared_i2c.cpp")
zambretti_h = read("src/zambretti_forecaster.h")
zambretti_cpp = read("src/zambretti_forecaster.cpp")
radar_h = read("src/radar_service.h")
radar_cpp = read("src/radar_service.cpp")
map_h = read("src/map_renderer.h")
map_cpp = read("src/map_renderer.cpp")
lightning_h = read("src/lightning_service.h")
lightning_cpp = read("src/lightning_service.cpp")
weather_cpp = read("src/weather_service.cpp")
ui_h = read("src/ui.h")
ui_cpp = read("src/ui.cpp")
patch = read("scripts/patch_display_driver.py")
readme = read("README.md")

require("0.30.9-adaptive-tls-guard" in version,
        "firmware version is not v0.30.9-adaptive-tls-guard")
require("DEFAULT_HOME_LAT" in config and "DEFAULT_HOME_LON" in config and
        "home_lat" in device_cpp and "home_lon" in device_cpp and
        "settings_.homeLat" in device_cpp and "settings_.homeLon" in device_cpp,
        "user-configurable HOME position is missing")
settings_defaults = read("include/settings_defaults.h")
require('#define WU_STATION_ID ""' in settings_defaults and
        '#define ADSB_AIRCRAFT_URL ""' in settings_defaults,
        "public defaults still contain private WU/ADS-B values")
require("fetchOpenMeteoCurrent" in weather_cpp and
        "Current weather: WU not configured, using Open-Meteo" in weather_cpp,
        "account-free Open-Meteo current weather fallback is missing")
require("ADSB_FI_BASE_URL" in config and "opendata.adsb.fi/api" in config and
        "ADSB_FI_RADIUS_NM = 180" in config and
        "ADSB_FI_REFRESH_MS = 10UL * 1000UL" in config,
        "adsb.fi Czech-wide source configuration is missing")
adsb_cpp = read("src/adsb_service.cpp")
require("fetchAdsbFi" in adsb_cpp and "local + %s" in adsb_cpp and
        "findAircraftByHex" in adsb_cpp and "mlatPosition" in adsb_cpp and
        "BasicJsonDocument<PsramAllocator>" in adsb_cpp,
        "hybrid local + adsb.fi merge/MLAT/PSRAM parsing is missing")
require("adsb_local_enabled" in device_cpp and "adsb_local_on" in device_cpp and
        "setLocalEnabled" in main and "ADSB_LOCAL_BACKOFF_AFTER_FAILURES = 3" in config and
        "ADSB_LOCAL_FAILURE_BACKOFF_MS = 30UL * 1000UL" in config and
        "consecutiveLocalFailures_" in adsb_cpp and "retry in %u s" in adsb_cpp,
        "local ADS-B enable switch / 3-failure 30-second backoff is missing")
require("WIFI_PROFILE_COUNT = 5" in device_h and
        "WifiProfile wifiProfiles[WIFI_PROFILE_COUNT]" in device_h and
        "wifi_multi" in device_cpp and "wifi_ssid%u" in device_cpp and
        "migrated legacy Wi-Fi to profile 1" in device_cpp and
        "trying profile %u/%u" in device_cpp and
        "Wi-Fi profile NVS read-back verification failed" in device_cpp and
        "wifi_enabled_" in device_cpp and "Wi-Fi profily" in device_cpp,
        "five-profile Wi-Fi storage/migration/failover UI is missing")

network_block = adsb_cpp.split("bool AdsbService::fetchNetworkProvider", 1)[1].split("bool AdsbService::fetchAdsbFi", 1)[0]
network_worker_h = read("src/network_worker.h")
network_worker_cpp = read("src/network_worker.cpp")
require("640U * 1024U" in network_block and "total=%u ac=%u" in network_block and
        "client.setTimeout(15000)" in network_block and "http.useHTTP10(true)" not in network_block and
        "downloadJsonBody" in adsb_cpp and "reinterpret_cast<char*>(body.data)" in network_block and
        "AdsbInternet" in network_worker_cpp and "applyNetworkSnapshot" in adsb_cpp,
        "ADS-B buffered HTTP/PSRAM worker integration is missing")
require("xTaskCreatePinnedToCore" in network_worker_cpp and
        '"network-worker"' in network_worker_cpp and
        "WeatherCurrent" in network_worker_cpp and "Radar" in network_worker_cpp and
        "requestAll" in main and "consumeRadar" in main and
        "weather.updateCurrent();" not in main.split("void loop()",1)[1] and
        "radar.updateFrames();" not in main.split("void loop()",1)[1],
        "runtime network operations are not isolated in the background worker")
require("TLS_GUARD_CRITICAL_FREE_INTERNAL" in config and
        "TLS_GUARD_CRITICAL_LARGEST_BLOCK" in config and
        "TLS_GUARD_RECOVER_LARGEST_BLOCK" in config and
        "TLS_RECOVERY_FAILURE_LARGEST_BLOCK" in config and
        "tlsPreflight" in network_worker_cpp and
        "requestReactiveTlsRecovery" in network_worker_cpp and
        "consumeTlsRecoveryRequest" in network_worker_cpp and
        "requestTransportYield" in lightning_cpp and
        "fallback suppressed code=%d" in adsb_cpp and
        "lastNetworkHttpCode_ = code" in adsb_cpp and
        "releaseSecureHttp" in adsb_cpp and
        "tls_memory_state" in device_cpp,
        "adaptive TLS resource guard / reactive transport-yield hardening is missing")
require("serviceNetwork" in device_h and "WiFi async: starting reconnect cycle" in device_cpp and
        "deviceConfig.serviceNetwork()" in main and "ensureNetwork(4000)" not in main,
        "runtime Wi-Fi reconnect is still blocking")
require("MAX_AIRCRAFT = 180" in config and "adsbFiCount" in models and
        "mlatCount" in models and "ADS-B: local + adsb.fi/adsb.lol" in map_cpp,
        "expanded hybrid ADS-B model or map attribution is missing")
require("showOtaScreen" in ui_cpp and "finishOtaScreen" in ui_cpp,
        "minimal OTA LCD screen is missing")
progress_start = main.find("case OtaDisplayEvent::Progress:")
progress_end = main.find("case OtaDisplayEvent::Success:", progress_start)
progress_block = main[progress_start:progress_end]
require(progress_start >= 0 and
        "setOtaBacklightRaw(false)" in progress_block and
        "lv_refr_now" not in progress_block and
        "lvgl_port_lock" not in progress_block and
        "esp_lcd_rgb_panel_restart" not in progress_block and
        "UI::" not in progress_block,
        "OTA progress callback must only blank the physical backlight")
require('server_.hasArg("size")' in device_cpp and
        "Update.begin(updateSize, U_FLASH)" in device_cpp and
        "otaBytesWritten_ != otaExpectedBytes_" in device_cpp,
        "OTA exact-size begin/final verification is missing")
require("OTA: reboot armed after successful Update.end" in device_cpp and
        "restartAt_ = millis() + 2200U" in device_cpp and
        "OTA: success response, reboot in 1 s" in device_cpp,
        "successful OTA does not arm an upload-end fallback reboot")
require("otaDisplayFailurePending_" in device_h and
        "Any OTA failure raised from the synchronous multipart callback" in device_cpp,
        "OTA failure display is not deferred out of the multipart callback")
require("setOtaBacklightRaw(false)" in main and
        "DISPLEJ ZHASNE - NEVYPINEJTE" in ui_cpp and
        "setTimeout(r,1100)" in device_cpp,
        "OTA RGB blackout/preflight behaviour is missing")
write_start = device_cpp.find("if (upload.status == UPLOAD_FILE_WRITE)")
write_end = device_cpp.find("if (upload.status == UPLOAD_FILE_END)", write_start)
write_block = device_cpp[write_start:write_end]
require("otaDisplayCallback_" not in write_block and
        "esp_lcd_rgb_panel_restart" not in write_block and
        "lv_refr_now" not in write_block,
        "OTA flash-write path must not perform display work")
require('setBacklight(true, "OTA update")' in main,
        "OTA update does not force LCD backlight on")
require("LIGHTNING_ALERT_RADIUS_KM = 10.0f" in config and
        "LIGHTNING_ALERT_MAX_AGE_SEC = 10UL * 60UL" in config,
        "10 km / 10 min lightning proximity alert constants are missing")
require("recentStrikeWithin" in lightning_h and
        "greatCircleDistanceKm" in lightning_cpp and
        "recentStrikeWithin(homeLat, homeLon" in main,
        "realtime lightning proximity detection is missing")
require("LIGHTNING_TRAIL_WHITE_MAX_AGE_SEC = 2UL * 60UL" in config and
        "LIGHTNING_TRAIL_YELLOW_MAX_AGE_SEC = 5UL * 60UL" in config and
        "LIGHTNING_TRAIL_ORANGE_MAX_AGE_SEC = 10UL * 60UL" in config and
        "LIGHTNING_TRAIL_RED_MAX_AGE_SEC = 20UL * 60UL" in config and
        "trailColorForAge" in lightning_h and
        "for (int band = 3; band >= 0; --band)" in lightning_cpp and
        "Config::LIGHTNING_TRAIL_RED_MAX_AGE_SEC + 120" in lightning_h,
        "20-minute colour-coded lightning trail is missing or history is too short")
require("LIGHTNING_FIRST_DATA_TIMEOUT_MS = 60UL * 1000UL" in config and
        "LIGHTNING_STALE_DATA_TIMEOUT_MS = 120UL * 1000UL" in config and
        "forceReconnect" in lightning_h and
        "no first JSON frame" in lightning_cpp and
        "no valid JSON data" in lightning_cpp and
        "lastValidFrameMs_ = lastSuccessMs_" in lightning_cpp,
        "LightningMaps valid-JSON watchdog/reconnect is missing")
require("ageSec <= Config::LIGHTNING_TRAIL_WHITE_MAX_AGE_SEC" in lightning_cpp and
        "Older trail entries are deliberately point-like" in lightning_cpp and
        "drawStrike(destination, width, height, x, y, color, ageSec)" in lightning_cpp,
        "compact fresh-bolt / point-like historical lightning markers are missing")

require("LIGHTNING_REDRAW_MS = 30UL * 1000UL" in config and
        "renderLive" in lightning_h and
        "LightningService::renderLive" in lightning_cpp and
        "const uint32_t ageSec = nowEpoch - strike.epochSec" in lightning_cpp and
        "radarFrameTimes_" not in lightning_h and
        "updateForRadar" not in lightning_cpp and
        "lightning.renderLive" in main,
        "independent realtime lightning rendering is missing")
require("drawGeographicCircle" in map_cpp and
        "LIGHTNING_ALERT_RADIUS_KM" in map_cpp and
        "lightningProximityAlert" in map_h,
        "geographic red 10 km lightning warning circle is missing")
require("drawLightningTrailLegend" in map_cpp and
        "0xFFFFFF, 0xFFE000, 0xFF8000, 0xFF2828" in map_cpp,
        "lightning age legend is missing")
require("qio_opi" in pio and "board_build.psram_type = opi" in pio,
        "8 MB OPI PSRAM configuration is missing")
require("BOUNCE_LINES = 20" in patch and "strict=False" in patch,
        "conservative non-fatal 20-line display patch is missing")
require("ESP_PANEL_LCD_RGB_CLK_HZ" not in patch,
        "display patch must not alter panel PCLK")

# Barometer hardware and shared-I2C implementation.
for dependency in (
    "Adafruit BMP280 Library",
    "Adafruit BMP085 Library",
    "Adafruit BME280 Library",
    "Adafruit Unified Sensor",
):
    require(dependency not in pio, f"unused dependency remains: {dependency}")
require("I2C_SDA_PIN = 8" in config and "I2C_SCL_PIN = 9" in config,
        "external I2C GPIO definition is missing")
require("BAROMETER_REFRESH_MS = 60UL * 1000UL" in config,
        "one-minute barometer refresh is missing")
require("PRESSURE_HISTORY_STEP_MS = 5UL * 60UL * 1000UL" in config,
        "five-minute pressure history interval is missing")
require("PRESSURE_HISTORY_POINT_COUNT = 289" in models,
        "24-hour pressure history capacity is missing")
require("projectedPressureHpa[3]" in models and "BarometerSnapshot" in models,
        "barometer data model or three projections are missing")
require("Bmp180SharedI2c" in barometer_h and "I2C_NUM_0, 0x77" in barometer_cpp,
        "BMP180 shared-I2C integration is missing")
require("i2c_master_write_read_device" in bmp180_cpp and
        "i2c_master_write_to_device" in bmp180_cpp,
        "BMP180 does not reuse the ESP-IDF I2C bus")
require("kBmp180ExpectedChipId = 0x55" in bmp180_cpp and
        "kCalibrationStartRegister = 0xAA" in bmp180_cpp,
        "BMP180 identification or calibration validation is missing")
require("Wire.begin" not in barometer_cpp and "Wire.begin" not in bmp180_cpp,
        "barometer must not reinitialize the display I2C bus")
require("lvgl_port_lock(500)" in main and
        "shared I2C lock timeout" in main,
        "GT911/BMP180 shared-bus serialization is missing")
require("regres" in readme.lower() and "zambretti" in readme.lower(),
        "README does not explain regression and Zambretti")
require("kTrendWindowMs = 3UL * 60UL * 60UL * 1000UL" in barometer_cpp,
        "three-hour regression window is missing")
require("kTrendWindowMs - Config::PRESSURE_HISTORY_STEP_MS" in barometer_cpp,
        "near-three-hour minimum trend collection interval is missing")
require("toSeaLevelPressure" in barometer_cpp and
        "selectReductionTemperature" in barometer_cpp and
        "venkovni prumer 12 h" in barometer_cpp and
        "powf(base, -5.257f)" in barometer_cpp and "barometerAltitudeM" in device_h,
        "WU-temperature-aware sea-level pressure conversion is missing")
require("setOutdoorTemperature" in barometer_h and
        "clearOutdoorTemperatureHistory" in barometer_h and
        "kOutdoorTemperatureWindowSec = 12UL * 60UL * 60UL" in barometer_cpp and
        "observationEpoch == lastOutdoorTemperatureEpoch_" in barometer_cpp and
        "firstOutdoorSample && snapshot_.historyCount > 0" in barometer_cpp and
        "barometerWuStationId != settings.wuStationId" in main,
        "12-hour de-duplicated WU temperature history is missing")
require("stationPressureHpa" in models and
        "snapshot_.history[i].stationPressureHpa" in barometer_cpp,
        "pressure trend is not isolated from temperature reduction")
require("standard 15 C" in barometer_cpp and
        "kOutdoorTemperatureMaxAgeSec" in barometer_cpp,
        "safe stale-WU fallback temperature is missing")
require("barometerOffsetHpa" in device_h,
        "barometer calibration offset is missing")
require("Kalibrace podle referencniho tlaku" in device_cpp and
        "baro_calculate" in device_cpp and
        "weather_pressure_hpa" in device_cpp and
        "barometer_altitude_m" in device_cpp and
        "Math.pow(p0/p,1/5.257)" in device_cpp,
        "web altitude calibration helper is incomplete")
require("recordHistory" in barometer_cpp and "calculateTrend" in barometer_cpp,
        "barometer history or trend calculation is missing")
require("LittleFS" not in barometer_cpp and "Preferences" not in barometer_cpp,
        "pressure history must remain RAM-only")
require("observedSpanMs >= kMinimumTrendSpanMs" in barometer_cpp and
        "projectedPressureHpa" in barometer_cpp,
        "early pressure projections are not suppressed")
require("latest.timestampMs = nowMs" not in barometer_cpp,
        "history timestamps are being moved between five-minute samples")

# Zambretti algorithm.
require("namespace Zambretti" in zambretti_h and "Result forecast" in zambretti_h,
        "Zambretti public API is missing")
require("kWindPressureAdjustmentHpa[16]" in zambretti_cpp,
        "16-point Zambretti wind correction is missing")
require("kRisingCodes" in zambretti_cpp and "kFallingCodes" in zambretti_cpp and
        "kSteadyCodes" in zambretti_cpp,
        "Zambretti rising/falling/steady lookup tables are missing")
require("0.1740f * (1031.40f - pressure)" in zambretti_cpp and
        "0.1553f * (1029.95f - pressure)" in zambretti_cpp and
        "0.2314f * (1030.81f - pressure)" in zambretti_cpp,
        "classic Zambretti equations are missing")
require("summerHalfYear" in zambretti_cpp and "3.2f" in zambretti_cpp,
        "Zambretti seasonal correction is missing")
require("forecastTextCs" in zambretti_cpp and "case 'Z'" in zambretti_cpp,
        "Czech A-Z forecast texts are incomplete")
require("updateZambretti" in barometer_cpp and "Zambretti::forecast" in barometer_cpp,
        "barometer service does not invoke Zambretti")
require("setWindDirection" in barometer_h and "windDirectionDeg" in weather_cpp,
        "optional WU wind direction input is missing")
require("snapshot_.current = CurrentWeather{}" in read("src/weather_service.h"),
        "old WU station temperature is not invalidated on station change")
require("zambrettiCode" in models and "zambrettiReady" in models,
        "Zambretti snapshot/diagnostics fields are missing")

# Forecast and pressure layout.
require("constexpr uint8_t kForecastHours[3] = {3, 6, 9}" in weather_cpp,
        "forecast targets are not +3/+6/+9 hours")
require("ForecastSlot slots[3]" in models,
        "weather snapshot still reserves the old six forecast slots")
require("forecast_hours=12" in weather_cpp,
        "Open-Meteo request was not reduced to the short horizon")
require("kForecastCardCount = 3" in ui_cpp,
        "UI does not use three forecast cards")
require("TLAK 24 H | ZAMBRETTI" in ui_cpp and "drawPressureGraph" in ui_cpp,
        "24-hour pressure graph or Zambretti title is missing")
require("historyColor" in ui_cpp and "forecastColor" in ui_cpp and
        "pressureLine" in ui_cpp,
        "history and projected pressure curves are missing")
require("void updatePressure(const BarometerSnapshot& barometer)" in ui_h and
        "void updatePressure(const BarometerSnapshot& barometer)" in ui_cpp,
        "pressure UI update API is missing")
require("updatePressureUi" in main and "barometer.update" in main,
        "barometer is not scheduled by the application")

# Web settings and diagnostics.
require("I2C barometr a Zambretti" in device_cpp,
        "barometer web settings card is missing")
require('preferences.putBool("baro_on"' in device_cpp and
        'preferences.putFloat("baro_alt"' in device_cpp and
        'preferences.putFloat("baro_off"' in device_cpp,
        "barometer settings are not persisted to NVS")
require("barometer_pressure_hpa" in device_cpp and
        "pressure_history_count" in device_cpp and
        "barometer_forecast" in device_cpp and
        "zambretti_code" in device_cpp and "zambretti_wind_used" in device_cpp and
        "barometer_reduction_temperature_c" in device_cpp and
        "barometer_raw_pressure_hpa" in device_cpp and
        "wu_temperature_average_c" in device_cpp,
        "barometer/Zambretti diagnostics JSON is incomplete")
require('server_.on("/diagnostics"' in device_cpp and
        'server_.on("/api/diagnostics"' in device_cpp,
        "web diagnostics routes are missing")
require("href='/diagnostics'" in device_cpp,
        "diagnostics link is missing from the main page")
require("RuntimeDiagnostics" in models and "updateRuntimeDiagnostics" in main,
        "runtime diagnostics data model is missing")

# Existing clock, backlight, network and map features must remain intact.
# Startup presentation.
require("beginStartup" in ui_h and "updateStartupStatus" in ui_h and
        "completeStartup" in ui_h and "showMainScreen" in ui_h,
        "startup screen public API is incomplete")
require("Vytvoril OK5TVR" in ui_cpp and
        "lv_timer_create(startupAnimationTimer" in ui_cpp and
        "gStartupProgressFill" in ui_cpp,
        "animated startup screen or creator credit is missing")
require("Pripravuji radarovou animaci v PSRAM" in main and
        "ADS-B a sitove sluzby se spusti na pozadi" in main and
        "UI::showMainScreen()" in main,
        "startup progress stages or final transition are missing")
require("gMainScreen = lv_obj_create(nullptr)" in ui_cpp,
        "main screen is not built off-screen during startup")

require("%H:%M:%S  %d.%m.%Y" in ui_cpp,
        "local clock/date display is missing")
require("CET-1CEST,M3.5.0/2,M10.5.0/3" in config and
        'setenv("TZ", Config::TZ_INFO, 1)' in main and "tzset()" in main,
        "automatic CET/CEST rule is missing")
require("BACKLIGHT_DAY_COUNT = 7" in device_h and
        "backlightWakeUntil = now + 60000U" in main,
        "seven-day backlight schedule or one-minute wake is missing")
require("setBacklightWakeOverlay" in ui_cpp and "LV_EVENT_RELEASED" in ui_cpp,
        "safe touch-wake overlay is missing")
require("WebServer" in device_h and "DNSServer" in device_h and
        "WiFi.softAP" in device_cpp and "dnsServer_.start" in device_cpp,
        "first-run AP and persistent web server are incomplete")
require("otadata" in read("partitions.csv") and
        "ota_0" in read("partitions.csv") and "ota_1" in read("partitions.csv"),
        "dual OTA partitions are missing")
require("#include <Update.h>" in device_cpp and
        'server_.on("/update"' in device_cpp and
        "UPLOAD_FILE_START" in device_cpp and
        "Update.begin(updateSize, U_FLASH)" in device_cpp and
        "Update.write" in device_cpp and "Update.end(true)" in device_cpp and
        "OTA aktualizace firmware" in device_cpp,
        "browser OTA update path is incomplete")
require("otaInProgress" in device_h and "deviceConfig.otaInProgress()" in main,
        "main loop is not protected during OTA flash writes")
require("AIRCRAFT_ALERT_SLOT_COUNT = 3" in models and
        "kAlertColors[AIRCRAFT_ALERT_SLOT_COUNT]" in map_cpp,
        "three aircraft highlights are missing")
require("radarLayerEnabled" in device_h and
        "lightningLayerEnabled" in device_h and
        "adsbLayerEnabled" in device_h,
        "radar/lightning/ADS-B layer settings are missing")
require("consumeMapTap" in main and "nextZoomMode" in main,
        "touch map zoom is missing")


# LightningMaps realtime plain-JSON lightning overlay.
require("links2004/WebSockets@2.7.2" in pio and
        "bblanchon/ArduinoJson@6.21.5" in pio,
        "WebSocket/ArduinoJson dependencies are missing")
require("class LightningService" in lightning_h and
        "WebSocketsClient" in lightning_h and
        "live2.lightningmaps.org" in lightning_cpp and
        "from_lightningmaps_org" in lightning_cpp and
        "buildSubscription" in lightning_cpp and
        "deserializeJson" in lightning_cpp and
        "DeserializationOption::Filter" in lightning_cpp and
        "timeMs / 1000ULL" in lightning_cpp and
        "decodeHeaderLzw" not in lightning_cpp and
        "ws7.blitzortung.org" not in lightning_cpp,
        "LightningMaps WebSocket/plain-JSON receive path is incomplete")
require("kMaxStrikes = 4096" in lightning_h and
        "MALLOC_CAP_SPIRAM" in lightning_cpp and
        "uint32_t id = 0" in lightning_h and
        "addStrike" in lightning_cpp and
        "pruneOldStrikes" in lightning_cpp,
        "LightningMaps PSRAM strike buffer is incomplete")
require('preferences.putBool("layer_lightning"' in device_cpp and
        'preferences.getBool("layer_lightning", true)' in device_cpp and
        "name='layer_lightning'" in device_cpp,
        "lightning layer is not persisted/configurable in the web UI")
require("lightning_status" in device_cpp and
        "lightning_ready" in device_cpp and
        "lightning_age_ms" in device_cpp and
        "lightning_layer" in device_cpp,
        "lightning web diagnostics are incomplete")
require("lastLightningUpdateMs" in models and
        "lightningReady" in models and
        "lightningStrikeCount" in models and
        "lightningStatus" in models,
        "lightning runtime diagnostics model is incomplete")
require("renderLive" in lightning_h and
        "renderLive" in lightning_cpp and
        "const uint32_t ageSec = nowEpoch - strike.epochSec" in lightning_cpp and
        "LIGHTNING_TRAIL_RED_MAX_AGE_SEC" in lightning_cpp and
        "updateForRadar" not in lightning_h and
        "radarFrameTimes_" not in lightning_h,
        "independent LightningMaps realtime render path is incomplete")
require("lightning.begin()" in main and
        "lightning.loop(" in main and
        "lightning.renderLive" in main and
        "lightning.updateForRadar(radar)" not in main and
        "(radarLayerEnabled || lightningLayerEnabled)" not in main,
        "lightning layer is still coupled to the radar timeline")
require("lightningLayerEnabled" in map_h and
        "lightningLayerEnabled" in map_cpp and
        "BLESKY" in map_cpp,
        "map footer/signature does not expose the lightning layer")
lightning_render = main.find("lightning.render")
reference_draw = main.find("MapRenderer::drawReference", lightning_render)
require(lightning_render >= 0 and reference_draw > lightning_render,
        "lightning must render below borders/cities/reference labels")
require("M_PI" not in lightning_cpp,
        "lightning service should not depend on non-portable M_PI")

# Radar/LCD protections retained from prior stable release.
require("setDisplayActive(true)" in main,
        "runtime RAM radar mode is not enabled")
require("updateFramesInMemory" in radar_h and "updateFramesInMemory" in radar_cpp,
        "RAM-only runtime radar update is missing")
require("downloadFileToMemory" in radar_cpp and "openRAM" in radar_cpp,
        "radar PNG is not downloaded and decoded from RAM")
require("esp_lcd_rgb_panel_restart" in main,
        "manual/post-operation RGB DMA recovery is missing")
require("DISPLAY_SYNC_RECOVERY_MS" not in config,
        "blind periodic LCD recovery must remain disabled")
require("DISPLAY_LOAD_GUARD_THRESHOLD_MS" in config and
        "DISPLAY_LOAD_GUARD_COOLDOWN_MS" in config and
        "scheduleDisplayRecoveryAfterLoad" in main,
        "load-triggered LCD recovery guard is missing")
require("loop blocked %u ms -> deferred RGB DMA resync" in main,
        "LCD load guard diagnostics are missing")
require(main.count("void setBacklight(bool on, const char* reason) {") == 1,
        "duplicate setBacklight definition detected")

m = re.search(r"bool RadarService::updateFramesInMemory\(\) \{(.*?)\n\}",
              radar_cpp, flags=re.S)
require(m is not None, "cannot locate updateFramesInMemory body")
if m:
    body = m.group(1)
    require("LittleFS." not in body and "removeIfExists" not in body,
            "runtime RAM radar update still touches LittleFS")

# Lightweight lexical and delimiter balance check.
def code_only(text):
    out = []
    i = 0
    state = "code"
    quote = ""
    escaped = False
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if c == "/" and n == "/":
                state = "line_comment"
                out.extend("  ")
                i += 2
                continue
            if c == "/" and n == "*":
                state = "block_comment"
                out.extend("  ")
                i += 2
                continue
            if c in ('"', "'"):
                state = "string"
                quote = c
                escaped = False
                out.append(" ")
                i += 1
                continue
            out.append(c)
            i += 1
            continue
        if state == "line_comment":
            if c == "\n":
                state = "code"
                out.append("\n")
            else:
                out.append(" ")
            i += 1
            continue
        if state == "block_comment":
            if c == "*" and n == "/":
                state = "code"
                out.extend("  ")
                i += 2
                continue
            out.append("\n" if c == "\n" else " ")
            i += 1
            continue
        if state == "string":
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == quote:
                state = "code"
            out.append("\n" if c == "\n" else " ")
            i += 1
    return "".join(out), state


for path in sorted(list((ROOT / "src").glob("*.cpp")) +
                   list((ROOT / "src").glob("*.h")) +
                   list((ROOT / "include").glob("*.h"))):
    raw = path.read_text(encoding="utf-8", errors="ignore")
    stripped, final_state = code_only(raw)
    require(final_state == "code",
            f"unterminated string/comment in {path.relative_to(ROOT)}")
    require(stripped.count("{") == stripped.count("}"),
            f"brace mismatch in {path.relative_to(ROOT)}")
    require(stripped.count("(") == stripped.count(")"),
            f"parenthesis mismatch in {path.relative_to(ROOT)}")
    require(stripped.count("[") == stripped.count("]"),
            f"bracket mismatch in {path.relative_to(ROOT)}")

if errors:
    print("STATIC AUDIT FAILED")
    for error in errors:
        print(" -", error)
    sys.exit(1)

print("STATIC AUDIT OK")
print("Target: Waveshare ESP32-S3-Touch-LCD-7 800x480")
print("Startup: animated status screen, progress and OK5TVR credit")
print("Forecast: three internet cards at +3/+6/+9 h")
print("Barometer: BMP180 on shared ESP-IDF I2C0 address 0x77")
print("Pressure: 12 h outdoor-temperature reduction with 15 C fallback")
print("History: 1 min samples, 5 min RAM points, 24 h sea-level chart")
print("Trend: 3 h regression from unreduced station pressure")
print("Zambretti: A-Z codes, season and optional weather wind correction")
print("Diagnostics: barometer, Zambretti and lightning status exposed on web")
print("Display: conservative 20-line buffer, no periodic DMA watchdog")
print("Radar: runtime PNG update remains RAM-only")
print("Lightning: LightningMaps plain-JSON WSS, viewport-filtered independent 20 min live overlay")

require("api.adsb.lol" in adsb_cpp, "adsb.lol fallback missing")
require("WiFi.hostByName" in adsb_cpp, "ADS-B DNS diagnostic missing")
require("HTTPClient::errorToString" in adsb_cpp, "ADS-B HTTP error text missing")
print("OTA: browser firmware upload to dual OTA app partitions")
