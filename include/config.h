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

// LightningMaps plain-JSON lightning is an independent realtime overlay, just
// like ADS-B. Radar animation changes only the CHMI image; lightning positions
// and colours are always evaluated against the real current time.
// MAX_Z_masked visually matches the CHMI web radar: echoes unlikely to reach
// the ground are shown with lighter, less saturated colours.
constexpr char RADAR_BASE_URL[] =
    "https://opendata.chmi.cz/meteorology/weather/radar/composite/maxz/png_masked/";
constexpr char RADAR_INDEX_URL[] =
    "https://opendata.chmi.cz/meteorology/weather/radar/composite/maxz/png_masked/";

// Generic Czech-Republic default used only before the first-run form is saved.
// The real HOME position is user-configurable and stored in NVS.
constexpr float DEFAULT_HOME_LAT = 49.8175f;
constexpr float DEFAULT_HOME_LON = 15.4730f;

// Lightning trail colours are evaluated against real current time. The same
// realtime lightning overlay remains visible while CHMI radar frames animate.
constexpr uint32_t LIGHTNING_TRAIL_WHITE_MAX_AGE_SEC = 2UL * 60UL;
constexpr uint32_t LIGHTNING_TRAIL_YELLOW_MAX_AGE_SEC = 5UL * 60UL;
constexpr uint32_t LIGHTNING_TRAIL_ORANGE_MAX_AGE_SEC = 10UL * 60UL;
constexpr uint32_t LIGHTNING_TRAIL_RED_MAX_AGE_SEC = 20UL * 60UL;
constexpr uint32_t LIGHTNING_REDRAW_MS = 30UL * 1000UL;
// live2.lightningmaps.org sends viewport-filtered JSON batches. Heartbeat checks
// the WSS transport; these longer guards reconnect the same endpoint if the
// socket remains open but valid JSON envelopes stop arriving.
constexpr uint32_t LIGHTNING_FIRST_DATA_TIMEOUT_MS = 60UL * 1000UL;
constexpr uint32_t LIGHTNING_STALE_DATA_TIMEOUT_MS = 120UL * 1000UL;
constexpr uint32_t LIGHTNING_WATCHDOG_RECONNECT_DELAY_MS = 2000UL;

// Realtime lightning proximity warning around the home/station position.
// A strike received in the last 10 minutes inside this 10 km radius makes
// the geographic 10 km warning circle around HOME turn red on the map.
constexpr float LIGHTNING_ALERT_RADIUS_KM = 10.0f;
constexpr uint32_t LIGHTNING_ALERT_MAX_AGE_SEC = 10UL * 60UL;

constexpr uint32_t ADSB_REFRESH_MS = 2000;
// Hybrid ADS-B: keep the fast local receiver and supplement the whole Czech
// map with adsb.fi Open Data. The public API allows up to 250 NM; 180 NM from
// the map centre covers the full configured Czech viewport with margin.
constexpr char ADSB_FI_BASE_URL[] = "https://opendata.adsb.fi/api";
constexpr char ADSB_LOL_BASE_URL[] = "https://api.adsb.lol";
constexpr float ADSB_FI_CENTER_LAT = 49.80f;
constexpr float ADSB_FI_CENTER_LON = 15.35f;
constexpr uint16_t ADSB_FI_RADIUS_NM = 180;
constexpr uint32_t ADSB_FI_REFRESH_MS = 10UL * 1000UL;
// Internet ADS-B is a realtime overlay. Do not let a temporary provider/TLS
// failure hide remote aircraft for several minutes: retries are capped at 60 s
// and an expired cache forces one recovery attempt at least every 30 s.
constexpr uint32_t ADSB_FI_RECOVERY_RETRY_MS = 30UL * 1000UL;
constexpr uint32_t ADSB_FI_BACKOFF_FIRST_MS = 15UL * 1000UL;
constexpr uint32_t ADSB_FI_BACKOFF_SECOND_MS = 30UL * 1000UL;
constexpr uint32_t ADSB_FI_BACKOFF_MAX_MS = 60UL * 1000UL;
// Keep the last good aircraft snapshots long enough to survive one bounded
// bulk HTTPS job in the serialized network worker. Source records themselves
// are still accepted only when seen_pos <= AIRCRAFT_MAX_AGE_SEC.
constexpr uint32_t ADSB_LOCAL_CACHE_MAX_AGE_MS = 60UL * 1000UL;
constexpr uint8_t ADSB_LOCAL_BACKOFF_AFTER_FAILURES = 3;
constexpr uint32_t ADSB_LOCAL_FAILURE_BACKOFF_MS = 30UL * 1000UL;
constexpr uint32_t ADSB_FI_CACHE_MAX_AGE_MS = 120UL * 1000UL;
constexpr uint32_t RADAR_REFRESH_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t RADAR_ANIMATION_MS = 1400;
// Personal-station observations refresh every 5 min; the 48 h hourly forecast hourly.
constexpr uint32_t CURRENT_WEATHER_REFRESH_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t FORECAST_REFRESH_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t ASTRONOMY_REFRESH_MS = 60UL * 1000UL;
constexpr uint32_t BAROMETER_REFRESH_MS = 60UL * 1000UL;
constexpr uint32_t PRESSURE_HISTORY_STEP_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t WIFI_RETRY_MS = 15UL * 1000UL;
// If several independent network feeds are stale while STA still reports
// connected, rebuild Wi-Fi and expose the configuration AP as a failsafe.
constexpr uint32_t NETWORK_GLOBAL_STALE_MS = 3UL * 60UL * 1000UL;
constexpr uint32_t NETWORK_RECOVERY_COOLDOWN_MS = 10UL * 60UL * 1000UL;

// New outbound HTTPS/TLS handshakes require a contiguous block of internal
// RAM even when several megabytes of PSRAM are free. When the internal heap is
// too fragmented, defer bulk TLS work instead of repeatedly opening sockets
// that will fail with HTTP -1 / mbedTLS connect errors. A short LightningMaps
// transport yield can free its existing TLS context before the next attempt.
constexpr uint32_t TLS_GUARD_MIN_FREE_INTERNAL = 48UL * 1024UL;
constexpr uint32_t TLS_GUARD_MIN_LARGEST_BLOCK = 36UL * 1024UL;
constexpr uint32_t TLS_GUARD_RETRY_MS = 5UL * 1000UL;
constexpr uint8_t TLS_GUARD_FORCE_AFTER_DEFERS = 4;
constexpr uint32_t TLS_GUARD_LIGHTNING_YIELD_MS = 20UL * 1000UL;
constexpr uint32_t TLS_POST_REQUEST_SETTLE_MS = 20UL;

// RGB LCD recovery guard. A blind periodic DMA restart was intentionally
// removed in v0.20.1 because frequent restarts made horizontal movement worse.
// Instead, recovery is scheduled only after an unusually long main-loop
// iteration (typically a slow HTTPS/PSRAM operation), and never more often
// than the cooldown below.
constexpr uint32_t DISPLAY_LOAD_GUARD_THRESHOLD_MS = 1500UL;
constexpr uint32_t DISPLAY_LOAD_GUARD_COOLDOWN_MS = 90UL * 1000UL;
// A background job does not lengthen Arduino loop(), but a very slow/failed
// TLS transfer can still create PSRAM/Wi-Fi contention. Request one deferred
// LCD resync only for exceptional jobs, using the same 90 s cooldown.
constexpr uint32_t NETWORK_LCD_RECOVERY_THRESHOLD_MS = 8000UL;

constexpr char CONFIG_HOSTNAME[] = "radar-adsb";
constexpr char CONFIG_AP_PREFIX[] = "Radar-ADSB-Setup";
constexpr char CONFIG_AP_PASSWORD[] = "radarsetup";

constexpr size_t MAX_AIRCRAFT = 180;
constexpr uint32_t AIRCRAFT_MAX_AGE_SEC = 30;
constexpr uint8_t I2C_SDA_PIN = 8;
constexpr uint8_t I2C_SCL_PIN = 9;

constexpr char TZ_INFO[] = "CET-1CEST,M3.5.0/2,M10.5.0/3";

}  // namespace Config
