#!/usr/bin/env python3
"""Focused static audit for v0.22.0 diagnostics, clock and weekly backlight schedule."""
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
radar_h = read("src/radar_service.h")
radar_cpp = read("src/radar_service.cpp")
map_h = read("src/map_renderer.h")
map_cpp = read("src/map_renderer.cpp")
weather_cpp = read("src/weather_service.cpp")
ui_cpp = read("src/ui.cpp")
patch = read("scripts/patch_display_driver.py")
readme = read("README.md")

require("0.22.0-weekly-backlight" in version,
        "firmware version is not v0.22.0")
require("qio_opi" in pio and "board_build.psram_type = opi" in pio,
        "8 MB OPI PSRAM configuration is missing")
require("BOUNCE_LINES = 20" in patch,
        "20-line bounce-buffer patch is missing")
require(r"\d+[uUlL]*" in patch and "strict=False" in patch,
        "display patch is not tolerant of stale numeric cache values")
require("ESP_PANEL_LCD_RGB_CLK_HZ" not in patch,
        "display patch must not alter panel PCLK")


require('server_.on("/diagnostics"' in device_cpp and
        'server_.on("/api/diagnostics"' in device_cpp,
        "web diagnostics routes are missing")
require("buildDiagnosticsPage" in device_h and
        "handleDiagnosticsJson" in device_h,
        "diagnostics page or JSON handler declaration is missing")
require("RuntimeDiagnostics" in models and
        "updateRuntimeDiagnostics" in main,
        "runtime diagnostics data model is missing")
require("%H:%M:%S  %d.%m.%Y" in ui_cpp and
        "RADAR CR + ADS-B" not in ui_cpp,
        "display header was not replaced by local clock and date")
require("CET-1CEST,M3.5.0/2,M10.5.0/3" in config and
        'setenv("TZ", Config::TZ_INFO, 1)' in main and "tzset()" in main,
        "automatic CET/CEST timezone rule is missing")


require("BACKLIGHT_DAY_COUNT = 7" in device_h and
        "BacklightDaySchedule" in device_h,
        "seven-day backlight schedule model is missing")
require('preferences.putBool("bl_sched"' in device_cpp and
        'preferences.putUShort(key, settings_.backlightDays[day].startMinutes)' in device_cpp and
        'preferences.putUShort(key, settings_.backlightDays[day].endMinutes)' in device_cpp,
        "backlight schedule is not persisted to NVS")
require("Plan podsviceni" in device_cpp and
        "bl_start_" in device_cpp and "bl_end_" in device_cpp,
        "weekly backlight web controls are missing")
require("dayScheduleActive" in main and
        "toggle_backlight" in main and
        "backlightWakeUntil = now + 60000U" in main,
        "backlight scheduling or one-minute touch wake is missing")
require("setBacklightWakeOverlay" in ui_cpp and
        "LV_EVENT_RELEASED" in ui_cpp,
        "safe touch-wake overlay is missing")
require("backlight_schedule_enabled" in device_cpp and
        "backlight_wake_remaining_ms" in device_cpp,
        "backlight diagnostics are missing")
require("href='/diagnostics'" in device_cpp,
        "diagnostics link is missing from the main page")

require("WebServer" in device_h and "DNSServer" in device_h,
        "web server or captive DNS declaration is missing")
require('kPreferencesNamespace[] = "devicecfg"' in device_cpp,
        "device configuration NVS namespace is missing")
require("WiFi.softAP" in device_cpp and "dnsServer_.start" in device_cpp,
        "first-run AP or captive DNS is missing")
require('server_.on("/save"' in device_cpp,
        "configuration save endpoint is missing")
require('server_.on("/api/status"' in device_cpp,
        "status JSON endpoint is missing")
require("started on port 80 for AP and STA" in device_cpp,
        "home-network web availability is not documented")
require("consumeRuntimeSettingsChanged" in device_h and
        "consumeRuntimeSettingsChanged" in main,
        "runtime web settings application is missing")
require("CONFIG_AP_PASSWORD" in config and "CONFIG_HOSTNAME" in config,
        "AP password or hostname is missing")

require("AIRCRAFT_ALERT_SLOT_COUNT = 3" in models,
        "three aircraft alert slots are missing")
require("aircraftAlertMatchIndex" in models,
        "multi-alert matching helper is missing")
require("aircraftAlertTargets[AIRCRAFT_ALERT_SLOT_COUNT]" in device_h,
        "three persistent aircraft targets are missing")
require('preferences.putString("alert_id0"' in device_cpp and
        'preferences.putString("alert_id1"' in device_cpp and
        'preferences.putString("alert_id2"' in device_cpp,
        "three aircraft targets are not saved to NVS")
require('String("alert_target_")' in device_cpp and
        "AIRCRAFT_ALERT_SLOT_COUNT" in device_cpp,
        "three aircraft fields are missing from the web UI")
require("kAlertColors[AIRCRAFT_ALERT_SLOT_COUNT]" in map_cpp,
        "three highlighted symbol colours are missing")
require("circleOutline" in map_cpp and "highlighted" in map_cpp,
        "highlighted aircraft symbol is missing")

require("radarLayerEnabled" in device_h and "adsbLayerEnabled" in device_h,
        "layer settings are missing")
require('preferences.putBool("layer_radar"' in device_cpp and
        'preferences.putBool("layer_adsb"' in device_cpp,
        "layer settings are not saved to NVS")
require("radarLayerEnabled && radar.frameCount()" in main,
        "radar rendering is not controlled by its layer switch")
require("if (adsbLayerEnabled)" in main,
        "ADS-B rendering is not controlled by its layer switch")
require("radarLayerEnabled, adsbLayerEnabled" in main,
        "layer state is not passed to the map renderer")
require("if (radarLayerEnabled) drawLegend" in map_cpp,
        "radar legend remains visible when radar is disabled")

require("setDisplayActive(true)" in main,
        "runtime RAM radar mode is not enabled")
require("updateFramesInMemory" in radar_h and "updateFramesInMemory" in radar_cpp,
        "RAM-only runtime radar update is missing")
require("downloadFileToMemory" in radar_cpp and "openRAM" in radar_cpp,
        "radar PNG is not downloaded and decoded from RAM")
require("if (displayActive_) return updateFramesInMemory();" in radar_cpp,
        "runtime update can still enter the LittleFS refresh path")
require("esp_lcd_rgb_panel_restart" in main,
        "post-operation RGB DMA resync is missing")
require("Preferences mapPreferences" in main and
        'mapPreferences.begin("mapview", false)' in main,
        "map viewport NVS persistence is missing")
require("consumeMapTap" in main and "nextZoomMode" in main,
        "touch zoom cycle is missing")
require("api.open-meteo.com/v1/forecast" in weather_cpp,
        "Open-Meteo forecast is missing")
require("LV_EVENT_CLICKED" in ui_cpp,
        "map touch event is missing")
require("radar-adsb.local" in readme and "tri letounu" in readme,
        "README does not document home web and three aircraft")

require('server_.on("/lcd-resync"' in device_cpp and
        "consumeLcdResyncRequested" in device_h and
        "consumeLcdResyncRequested" in main,
        "manual web LCD recovery is missing")
require("periodic LCD watchdog" not in main and
        "DISPLAY_SYNC_RECOVERY_MS" not in config,
        "periodic LCD watchdog must remain disabled")

# Runtime radar function must not call filesystem APIs.
m = re.search(r"bool RadarService::updateFramesInMemory\(\) \{(.*?)\n\}",
              radar_cpp, flags=re.S)
require(m is not None, "cannot locate updateFramesInMemory body")
if m:
    body = m.group(1)
    require("LittleFS." not in body and "removeIfExists" not in body,
            "runtime RAM update still touches LittleFS")

# Lightweight lexical and brace balance check.
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

if errors:
    print("STATIC AUDIT FAILED")
    for error in errors:
        print(" -", error)
    sys.exit(1)

print("STATIC AUDIT OK")
print("Network: first-run AP plus persistent STA web UI")
print("Clock: local date/time with automatic CET/CEST")
print("Diagnostics: live web page, JSON API and backlight state")
print("Aircraft: three exact callsign/ICAO visual highlights")
print("Layers: radar and ADS-B independently switchable")
print("Backlight: seven-day schedule with 60-second touch wake")
print("Radar: runtime PNG update remains RAM-only")
