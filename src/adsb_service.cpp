#include "adsb_service.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "config.h"

namespace {
bool isInsideMap(float lat, float lon) {
  return lat <= Config::MAP_LAT_TOP && lat >= Config::MAP_LAT_BOTTOM &&
         lon >= Config::MAP_LON_LEFT && lon <= Config::MAP_LON_RIGHT;
}
}  // namespace

AdsbService::AdsbService(const char* aircraftUrl) : aircraftUrl_(aircraftUrl) {}

bool AdsbService::update() {
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(snapshot_.status, sizeof(snapshot_.status), "ADSB: WiFi offline");
    return false;
  }

  if (fetch()) return true;

  // fetch() stores the exact HTTP/JSON reason. Do not replace it with a
  // generic message; the detailed status is essential for local-network
  // diagnostics on the display.
  snapshot_.valid = false;
  return false;
}

bool AdsbService::fetch() {
  HTTPClient http;
  const String& url = aircraftUrl_;
  http.setConnectTimeout(4000);
  http.setTimeout(7000);
  if (!http.begin(url)) {
    snprintf(snapshot_.status, sizeof(snapshot_.status),
             "ADSB: nelze otevrit URL");
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(snapshot_.status, sizeof(snapshot_.status), "ADSB HTTP %d", code);
    http.end();
    return false;
  }

  StaticJsonDocument<512> filter;
  filter["now"] = true;
  filter["messages"] = true;
  JsonArray aircraftFilter = filter.createNestedArray("aircraft");
  JsonObject itemFilter = aircraftFilter.createNestedObject();
  itemFilter["hex"] = true;
  itemFilter["flight"] = true;
  itemFilter["lat"] = true;
  itemFilter["lon"] = true;
  itemFilter["track"] = true;
  itemFilter["gs"] = true;
  itemFilter["alt_baro"] = true;
  itemFilter["seen_pos"] = true;

  DynamicJsonDocument doc(64 * 1024);
  const DeserializationError err = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();

  if (err) {
    snprintf(snapshot_.status, sizeof(snapshot_.status), "ADSB JSON: %s",
             err.c_str());
    return false;
  }

  AircraftSnapshot next;
  next.generated = doc["now"] | 0;
  JsonArrayConst array = doc["aircraft"].as<JsonArrayConst>();

  for (JsonObjectConst source : array) {
    if (next.count >= Config::MAX_AIRCRAFT) break;
    if (source["lat"].isNull() || source["lon"].isNull()) continue;

    const float lat = source["lat"].as<float>();
    const float lon = source["lon"].as<float>();
    const float seen = source["seen_pos"] | 9999.0f;
    if (!isInsideMap(lat, lon) || seen > Config::AIRCRAFT_MAX_AGE_SEC) continue;

    Aircraft& target = next.items[next.count++];
    strlcpy(target.hex, source["hex"] | "--------", sizeof(target.hex));

    String flight = source["flight"] | "";
    flight.trim();
    strlcpy(target.flight, flight.c_str(), sizeof(target.flight));
    target.lat = lat;
    target.lon = lon;
    target.trackDeg = source["track"] | 0.0f;
    target.groundSpeedKt = source["gs"] | 0.0f;
    target.seenPositionSec = seen;

    JsonVariantConst altitude = source["alt_baro"];
    if (altitude.is<int32_t>() || altitude.is<float>()) {
      target.altitudeFt = altitude.as<int32_t>();
    } else {
      target.altitudeFt = -1;
    }
  }

  next.valid = true;
  strlcpy(next.endpoint, "/data/aircraft.json", sizeof(next.endpoint));
  snprintf(next.status, sizeof(next.status), "ADSB: %u aircraft",
           static_cast<unsigned>(next.count));
  snapshot_ = next;
  return true;
}
