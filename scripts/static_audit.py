#!/usr/bin/env python3
"""Focused static audit for v0.28.5 Blitzortung proximity alert + stable OTA screen."""
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

require("0.28.5-ota-screen" in version,
        "firmware version is not v0.28.5-ota-screen")
require("showOtaScreen" in ui_cpp and "finishOtaScreen" in ui_cpp,
        "minimal OTA LCD screen is missing")
require("OtaDisplayEvent::Progress" in main and
        "esp_lcd_rgb_panel_restart" in main,
        "OTA progress does not resynchronise RGB panel DMA")
require("64U * 1024U" in device_cpp and "otaDisplayCallback_" in device_cpp,
        "OTA display refresh throttling/callback is missing")
require('setBacklight(true, "OTA update")' in main,
        "OTA update does not force LCD backlight on")
require("LIGHTNING_ALERT_RADIUS_KM = 10.0f" in config and
        "LIGHTNING_ALERT_MAX_AGE_SEC = 10UL * 60UL" in config,
        "10 km / 10 min lightning proximity alert constants are missing")
require("recentStrikeWithin" in lightning_h and
        "greatCircleDistanceKm" in lightning_cpp and
        "recentStrikeWithin(Config::FALLBACK_LAT, Config::FALLBACK_LON" in main,
        "realtime lightning proximity detection is missing")
require("drawGeographicCircle" in map_cpp and
        "LIGHTNING_ALERT_RADIUS_KM" in map_cpp and
        "lightningProximityAlert" in map_h,
        "geographic red 10 km lightning warning circle is missing")
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
        "WU prumer 12 h" in barometer_cpp and
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
        "Nacitam letouny ADS-B" in main and
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
        "Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)" in device_cpp and
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


# Blitzortung realtime lightning overlay.
require("links2004/WebSockets@2.7.2" in pio,
        "arduinoWebSockets dependency is missing")
require("class LightningService" in lightning_h and
        "WebSocketsClient" in lightning_h and
        "ws7.blitzortung.org" in lightning_cpp and
        "kSubscription" in lightning_cpp and
        "decodeHeaderLzw" in lightning_cpp and
        "timeNs / 1000000000ULL" in lightning_cpp,
        "Blitzortung WebSocket/LZW receive path is incomplete")
require("kMaxStrikes = 4096" in lightning_h and
        "MALLOC_CAP_SPIRAM" in lightning_cpp and
        "addStrike" in lightning_cpp and
        "pruneOldStrikes" in lightning_cpp,
        "Blitzortung PSRAM strike buffer is incomplete")
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
        "lightningFrameCount" in models and
        "lightningStatus" in models,
        "lightning runtime diagnostics model is incomplete")
require("frameTimeUtc" in radar_h and "frameTimeUtc" in radar_cpp,
        "radar frame timestamps are not exposed for lightning sync")
require("updateForRadar" in lightning_h and
        "Config::RADAR_STEP_SECONDS" in lightning_cpp and
        "renderFrame" in lightning_h and
        "renderFrame" in lightning_cpp and
        "strike.epochSec <= start" in lightning_cpp and
        "strike.epochSec > end" in lightning_cpp,
        "synchronized Blitzortung five-minute animation path is incomplete")
require("lightning.begin()" in main and
        "lightning.updateForRadar(radar)" in main and
        "lightning.loop(" in main and
        "lightning.renderFrame" in main and
        "(radarLayerEnabled || lightningLayerEnabled)" in main,
        "lightning animation is not coupled to the radar timeline")
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
require("periodic LCD watchdog" not in main and
        "DISPLAY_SYNC_RECOVERY_MS" not in config,
        "periodic LCD watchdog must remain disabled")
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
print("Pressure: WU 12 h outdoor-temperature reduction with 15 C fallback")
print("History: 1 min samples, 5 min RAM points, 24 h sea-level chart")
print("Trend: 3 h regression from unreduced station pressure")
print("Zambretti: A-Z codes, season and optional WU wind correction")
print("Diagnostics: barometer, Zambretti and lightning status exposed on web")
print("Display: conservative 20-line buffer, no periodic DMA watchdog")
print("Radar: runtime PNG update remains RAM-only")
print("Lightning: Blitzortung realtime WSS, LZW decode, six synchronized radar slots")
print("OTA: browser firmware upload to dual OTA app partitions")
