#pragma once

// Optional compile-time defaults. The firmware no longer requires secrets.h:
// Wi-Fi credentials are entered through the first-run access point.
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WU_API_KEY
#define WU_API_KEY ""
#endif

#ifndef WU_STATION_ID
#define WU_STATION_ID "IPLZE179"
#endif

#ifndef ADSB_AIRCRAFT_URL
#define ADSB_AIRCRAFT_URL "http://192.168.1.170:8080/data/aircraft.json"
#endif
