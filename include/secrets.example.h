#pragma once

// This file is optional in v0.18.0. Wi-Fi is always configured through the
// first-run access point and then stored in NVS.
// Copy to secrets.h only when you want compile-time defaults for WU and ADS-B.
#define WU_API_KEY "YOUR_WEATHER_UNDERGROUND_API_KEY"
#define WU_STATION_ID ""
#define ADSB_AIRCRAFT_URL ""
