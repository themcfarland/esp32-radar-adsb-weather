#!/usr/bin/env python3
"""Focused static audit for v23 RAM-only runtime radar updates."""
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
weather_cpp = (ROOT / "src/weather_service.cpp").read_text(encoding="utf-8")
ui_cpp = (ROOT / "src/ui.cpp").read_text(encoding="utf-8")
patch = (ROOT / "scripts/patch_display_driver.py").read_text(encoding="utf-8")

require("0.17.0-radar-ram-sync" in version,
        "firmware version was not updated")
require("qio_opi" in pio and "board_build.psram_type = opi" in pio,
        "8 MB OPI PSRAM configuration is missing")
require("RGB bounce buffer = 20 lines" in patch,
        "20-line bounce-buffer patch is missing")
require("ESP_PANEL_LCD_RGB_CLK_HZ" not in patch,
        "display patch must not alter panel PCLK")
require("setDisplayActive(true)" in main,
        "runtime RAM mode is not enabled after LCD initialization")
require("updateFramesInMemory" in radar_h and "updateFramesInMemory" in radar_cpp,
        "RAM-only runtime radar update is missing")
require("downloadFileToMemory" in radar_cpp and "openRAM" in radar_cpp,
        "radar PNG is not downloaded and decoded from RAM")
require("runtimeOverlayTarget_" in radar_h and "runtimeLineBuffer_" in radar_h,
        "line-by-line compact runtime decode is missing")
require("if (displayActive_) return updateFramesInMemory();" in radar_cpp,
        "runtime update can still enter the LittleFS refresh path")
require("removeIfExists" in radar_cpp,
        "safe LittleFS cleanup is missing")
require("runtime RAM-only update; LittleFS will not be touched" in radar_cpp,
        "runtime diagnostic message is missing")
require("esp_lcd_rgb_panel_restart" in main,
        "targeted post-operation RGB DMA resync is missing")
require("displayResyncPending" in main,
        "resync is not deferred until the operation and redraw finish")
require("Preferences mapPreferences" in main and
        'mapPreferences.begin("mapview", false)' in main,
        "map viewport NVS persistence is missing")
require("consumeMapTap" in main and "nextZoomMode" in main,
        "touch zoom cycle is missing")
require("api.open-meteo.com/v1/forecast" in weather_cpp,
        "Open-Meteo forecast is missing")
require("LV_EVENT_CLICKED" in ui_cpp,
        "map touch event is missing")
require("if ((y & 0x0F) == 0x0F) delay(1);" in map_cpp,
        "map rendering does not yield for RGB DMA")
require("Preloading CHMI radar files before LCD initialization" in main,
        "initial persistent radar refresh is not before LCD startup")
require("RADAR_REFRESH_MS = 5UL * 60UL * 1000UL" in config,
        "radar refresh interval changed unexpectedly")

# Runtime function must not call filesystem APIs.
m = re.search(r"bool RadarService::updateFramesInMemory\(\) \{(.*?)\n\}",
              radar_cpp, flags=re.S)
require(m is not None, "cannot locate updateFramesInMemory body")
if m:
    body = m.group(1)
    require("LittleFS." not in body and "removeIfExists" not in body,
            "runtime RAM update still touches LittleFS")

# Lightweight brace balance after removing strings/comments.
def strip_cpp(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = re.sub(r'//.*', '', text)
    text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
    text = re.sub(r"'(?:\\.|[^'\\])*'", "''", text)
    return text

for path in sorted(list((ROOT / "src").glob("*.cpp")) +
                   list((ROOT / "src").glob("*.h"))):
    stripped = strip_cpp(path.read_text(encoding="utf-8", errors="ignore"))
    require(stripped.count("{") == stripped.count("}"),
            f"brace mismatch in {path.relative_to(ROOT)}")

if errors:
    print("STATIC AUDIT FAILED")
    for error in errors:
        print(" -", error)
    sys.exit(1)

print("STATIC AUDIT OK")
print("Runtime radar: newest PNG in PSRAM, direct line-by-line RGB332 decode")
print("Runtime filesystem writes: disabled")
print("Display recovery: one VSYNC-scheduled RGB DMA restart after operation")
print("Map: touch zoom CR/50/25/10 km with NVS restore")
