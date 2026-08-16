#pragma once
#include <Arduino.h>

namespace Config {

constexpr uint16_t SCREEN_W = 800;
constexpr uint16_t SCREEN_H = 480;
constexpr uint16_t SIDEBAR_W = 200;
constexpr uint16_t MAP_W = SCREEN_W - SIDEBAR_W;
constexpr uint16_t HEADER_H = 36;
constexpr uint16_t MAP_H = SCREEN_H - HEADER_H;

static_assert(SCREEN_W == 800 && SCREEN_H == 480,
              "This project targets Waveshare ESP32-S3-Touch-LCD-7 only");
static_assert(MAP_W + SIDEBAR_W == SCREEN_W, "Invalid horizontal layout");
static_assert(MAP_H + HEADER_H == SCREEN_H, "Invalid vertical layout");

// Visible map: entire Czech Republic with a small surrounding margin.
constexpr float MAP_LON_LEFT   = 11.70f;
constexpr float MAP_LON_RIGHT  = 19.00f;
constexpr float MAP_LAT_TOP    = 51.30f;
constexpr float MAP_LAT_BOTTOM = 48.30f;

// Official CHMI PNG calibration. The images use Web Mercator (EPSG:3857).
// The full PNG extends farther east than the actual radar data field.
constexpr float RADAR_LON_LEFT   = 11.267f;
constexpr float RADAR_LON_RIGHT  = 20.770f;
constexpr float RADAR_LAT_TOP    = 52.167f;
constexpr float RADAR_LAT_BOTTOM = 48.047f;
constexpr uint16_t RADAR_SOURCE_MAX_W = 1200;
constexpr uint16_t RADAR_SOURCE_MAX_H = 900;
constexpr uint8_t RADAR_FRAME_COUNT = 6;
constexpr uint8_t RADAR_LOOKBACK_STEPS = 48;  // 4 hours in five-minute steps.
constexpr uint32_t RADAR_STEP_SECONDS = 5UL * 60UL;

// Blitzortung realtime lightning arrives over WSS. Historical strikes are
// rendered only in their matching five-minute CHMI radar slot. The newest
// radar frame also receives a short realtime overlay so a fresh strike appears
// immediately without accumulating a long stationary trail.
// MAX_Z_masked visually matches the CHMI web radar: echoes unlikely to reach
// the ground are shown with lighter, less saturated colours.
constexpr char RADAR_BASE_URL[] =
    "https://opendata.chmi.cz/meteorology/weather/radar/composite/maxz/png_masked/";
constexpr char RADAR_INDEX_URL[] =
    "https://opendata.chmi.cz/meteorology/weather/radar/composite/maxz/png_masked/";

// Used only if the station response does not contain coordinates.
constexpr float FALLBACK_LAT = 49.7863f;
constexpr float FALLBACK_LON = 13.2850f;

// Lightning colours are evaluated against the newest CHMI radar timestamp
// for historical slots. The short live overlay uses real current time. This
// keeps the colour trail while each strike belongs to only one radar frame.
constexpr uint32_t LIGHTNING_TRAIL_WHITE_MAX_AGE_SEC = 2UL * 60UL;
constexpr uint32_t LIGHTNING_TRAIL_YELLOW_MAX_AGE_SEC = 5UL * 60UL;
constexpr uint32_t LIGHTNING_TRAIL_ORANGE_MAX_AGE_SEC = 10UL * 60UL;
constexpr uint32_t LIGHTNING_TRAIL_RED_MAX_AGE_SEC = 20UL * 60UL;
constexpr uint32_t LIGHTNING_REALTIME_OVERLAY_MAX_AGE_SEC = 5UL * 60UL;

// Realtime lightning proximity warning around the home/station position.
// A strike received in the last 10 minutes inside this 10 km radius makes
// the geographic 10 km warning circle around HOME turn red on the map.
constexpr float LIGHTNING_ALERT_RADIUS_KM = 10.0f;
constexpr uint32_t LIGHTNING_ALERT_MAX_AGE_SEC = 10UL * 60UL;

constexpr uint32_t ADSB_REFRESH_MS = 2000;
constexpr uint32_t RADAR_REFRESH_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t RADAR_ANIMATION_MS = 1400;
// Personal-station observations refresh every 5 min; the 48 h hourly forecast hourly.
constexpr uint32_t CURRENT_WEATHER_REFRESH_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t FORECAST_REFRESH_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t ASTRONOMY_REFRESH_MS = 60UL * 1000UL;
constexpr uint32_t BAROMETER_REFRESH_MS = 60UL * 1000UL;
constexpr uint32_t PRESSURE_HISTORY_STEP_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t WIFI_RETRY_MS = 15UL * 1000UL;

constexpr char CONFIG_HOSTNAME[] = "radar-adsb";
constexpr char CONFIG_AP_PREFIX[] = "Radar-ADSB-Setup";
constexpr char CONFIG_AP_PASSWORD[] = "radarsetup";

constexpr size_t MAX_AIRCRAFT = 80;
constexpr uint32_t AIRCRAFT_MAX_AGE_SEC = 30;
constexpr uint8_t I2C_SDA_PIN = 8;
constexpr uint8_t I2C_SCL_PIN = 9;

constexpr char TZ_INFO[] = "CET-1CEST,M3.5.0/2,M10.5.0/3";

}  // namespace Config
