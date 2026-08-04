#!/usr/bin/env python3
"""Focused static audit for v22 touch map zoom and NVS persistence."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
errors = []

def require(condition, message):
    if not condition:
        errors.append(message)

pio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
config = (ROOT / "include/config.h").read_text(encoding="utf-8")
version = (ROOT / "include/version.h").read_text(encoding="utf-8")
main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
radar_h = (ROOT / "src/radar_service.h").read_text(encoding="utf-8")
radar_cpp = (ROOT / "src/radar_service.cpp").read_text(encoding="utf-8")
map_cpp = (ROOT / "src/map_renderer.cpp").read_text(encoding="utf-8")
patch = (ROOT / "scripts/patch_display_driver.py").read_text(encoding="utf-8")
all_code = "\n".join(
    p.read_text(encoding="utf-8", errors="ignore")
    for p in list((ROOT / "src").glob("*.[ch]pp"))
    + list((ROOT / "src").glob("*.h"))
    + list((ROOT / "include").glob("*.h"))
)

require("extra_scripts = pre:scripts/patch_display_driver.py" in pio,
        "display pre-build patch is not enabled")
require("build_src_filter = +<*> -<hardware_test.cpp>" in pio,
        "main source filter does not exclude hardware_test.cpp")
require("build_src_filter = -<*> +<hardware_test.cpp>" in pio,
        "test source filter is invalid")
require("qio_opi" in pio and "board_build.psram_type = opi" in pio,
        "8 MB OPI PSRAM configuration is missing")
require("lib_ldf_mode = deep" in pio and "deep+" not in pio,
        "LDF mode must be deep without plus")
require("RADAR_ANIMATION_MS = 1400" in config,
        "radar animation interval is not 1400 ms")
require("0.16.0-touch-map-zoom-nvs" in version,
        "firmware version was not updated")
weather_cpp = (ROOT / "src/weather_service.cpp").read_text(encoding="utf-8")
ui_cpp = (ROOT / "src/ui.cpp").read_text(encoding="utf-8")
require("/v3/wx/forecast/hourly/" in weather_cpp and
        'fetchHourlyForecast("2day"' in weather_cpp,
        "WU/TWC 2-day hourly forecast is missing")
require("api.open-meteo.com/v1/forecast" in weather_cpp and
        "forecast_hours=55" in weather_cpp,
        "Open-Meteo hourly fallback is missing")
require("http.useHTTP10(true)" in weather_cpp and
        'http.addHeader("Accept-Encoding", "identity")' in weather_cpp and
        "String payload = http.getString()" in weather_cpp and
        "deserializeJson(doc, payload)" in weather_cpp,
        "decoded Open-Meteo HTTP body handling is missing")
require(weather_cpp.find("fetchOpenMeteoForecast(openMeteoCode)") <
        weather_cpp.find('fetchHourlyForecast("2day", code2)'),
        "Open-Meteo must be attempted before unauthorized WU hourly products")
require("DebugLog::begin(115200)" in main and "HEARTBEAT" in main,
        "dual-port startup logging or heartbeat is missing")
require("kForecastHours[6] = {3, 6, 9, 12, 24, 48}" in weather_cpp,
        "requested hourly lead times are missing")
require("PREDPOVED 48 H" in ui_cpp and "temperatureC" in ui_cpp,
        "48-hour forecast GUI is missing")
require("Preferences mapPreferences" in main and
        'mapPreferences.begin("mapview", false)' in main and
        'mapPreferences.putUChar("mode"' in main and
        'mapPreferences.putFloat("lat"' in main and
        'mapPreferences.putFloat("lon"' in main,
        "map viewport NVS persistence is missing")
require("consumeMapTap" in main and "nextZoomMode" in main and
        "MapZoomMode::Km50" in map_cpp and "MapZoomMode::Km25" in map_cpp and
        "MapZoomMode::Km10" in map_cpp,
        "touch zoom cycle full/50/25/10 km is missing")
require("const MapViewport& viewport" in radar_h and
        "sourceXByMapX" in radar_cpp and "sourceYByMapY" in radar_cpp,
        "radar overlay is not remapped to the selected viewport")
require("LV_OBJ_FLAG_CLICKABLE" in ui_cpp and "LV_EVENT_CLICKED" in ui_cpp,
        "map canvas touch event is missing")
require("CURRENT_WEATHER_REFRESH_MS" in config and "FORECAST_REFRESH_MS" in config,
        "current and forecast refresh intervals are not separated")
require("prepareAnimationCache" in radar_h and "prepareAnimationCache" in radar_cpp,
        "compact radar animation cache is missing")
require("if (radar.updateFrames()) prepareRadarAnimation();" in main,
        "manual radar refresh does not rebuild compact cache")
require("Preloading CHMI radar files before LCD initialization" in main,
        "initial radar download is not placed before LCD startup")
require("if ((y & 0x0F) == 0x0F) delay(1);" in map_cpp,
        "map rendering does not yield for RGB DMA")
require("RGB bounce buffer = 20 lines" in patch,
        "20-line bounce-buffer patch is missing")
require("patch_display_driver(strict=True)" in patch,
        "display patch is not executed strictly during pre-build")
require("AddPreAction" not in patch and "patch_before_build" not in patch,
        "late SCons buildprog callback must not be registered")
require("ESP_PANEL_LCD_RGB_CLK_HZ" not in patch,
        "patch must not alter the working panel PCLK")
require("esp_lcd_rgb_panel_restart" not in all_code,
        "runtime RGB restart is forbidden")
require("esp_lcd_rgb_panel_set_pclk" not in all_code,
        "runtime PCLK change is forbidden")
require("LCD_SAFE_PCLK" not in all_code,
        "obsolete runtime safe-PCLK logic remains")

# Lightweight brace balance after removing strings/comments. This is not a C++
# parser, but catches accidental truncation while packaging.
def strip_cpp(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = re.sub(r'//.*', '', text)
    text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
    text = re.sub(r"'(?:\\.|[^'\\])*'", "''", text)
    return text

for path in sorted(list((ROOT / "src").glob("*.cpp")) + list((ROOT / "src").glob("*.h"))):
    stripped = strip_cpp(path.read_text(encoding="utf-8", errors="ignore"))
    require(stripped.count("{") == stripped.count("}"),
            f"brace mismatch in {path.relative_to(ROOT)}")

if errors:
    print("STATIC AUDIT FAILED")
    for error in errors:
        print(" -", error)
    sys.exit(1)

print("STATIC AUDIT OK")
print("Target: Waveshare ESP32-S3-Touch-LCD-7 800x480")
print("Panel timing: original v9 / 16 MHz, no runtime restart")
print("RGB bounce buffer: 20 lines")
print("Radar animation: compact cache, 1400 ms")
print("Forecast: Open-Meteo primary, WU fallback; +3/+6/+9/+12/+24/+48 h")
print("Diagnostics: USB CDC + UART0 heartbeat every 10 s")
print("Map: geoBoundaries CZE ADM0, 300 points; touch zoom CR/50/25/10 km with NVS restore")
