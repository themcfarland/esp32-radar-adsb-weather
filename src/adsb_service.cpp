#include "adsb_service.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <strings.h>
#include <string.h>
#include <time.h>
#include <new>

#include "config.h"
#include "debug_log.h"
#include "psram_allocator.h"

namespace {


struct PsramHttpBody {
  uint8_t* data = nullptr;
  size_t size = 0;

  ~PsramHttpBody() {
    if (data) heap_caps_free(data);
  }

  bool allocate(size_t capacity) {
    if (data) {
      heap_caps_free(data);
      data = nullptr;
      size = 0;
    }
    data = static_cast<uint8_t*>(
        heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return data != nullptr;
  }
};

void releaseSecureHttp(HTTPClient& http, WiFiClientSecure& client) {
  // Explicitly tear down both layers. On long-running ESP32 builds this is
  // more deterministic than relying only on local destructors and gives lwIP
  // a short window to return socket/TLS buffers to the internal heap.
  http.end();
  client.stop();
  delay(Config::TLS_POST_REQUEST_SETTLE_MS);
}

bool downloadJsonBody(HTTPClient& http, const char* providerLabel,
                      int contentLength, PsramHttpBody& body) {
  constexpr size_t kUnknownBodyCapacity = 1024U * 1024U;
  constexpr size_t kMaximumBodyBytes = 1024U * 1024U;
  constexpr uint32_t kNoDataTimeoutMs = 12000UL;
  constexpr uint32_t kTotalTimeoutMs = 30000UL;

  const bool knownLength = contentLength >= 0;
  const size_t expected = knownLength ? static_cast<size_t>(contentLength) : 0U;
  if (knownLength && expected > kMaximumBodyBytes) {
    DebugLog::printf("%s: body too large: %u B\n", providerLabel,
                     static_cast<unsigned>(expected));
    return false;
  }

  const size_t capacity = knownLength ? expected + 1U : kUnknownBodyCapacity + 1U;
  if (!body.allocate(capacity)) {
    DebugLog::printf("%s: PSRAM body allocation failed: %u B, free=%u largest=%u\n",
                     providerLabel, static_cast<unsigned>(capacity),
                     static_cast<unsigned>(ESP.getFreePsram()),
                     static_cast<unsigned>(heap_caps_get_largest_free_block(
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  const uint32_t startedMs = millis();
  uint32_t lastDataMs = startedMs;
  uint32_t nextProgress = 64U * 1024U;

  while (true) {
    if (knownLength && body.size >= expected) break;
    if (!knownLength && body.size >= kUnknownBodyCapacity) {
      DebugLog::printf("%s: body exceeded %u B limit\n", providerLabel,
                       static_cast<unsigned>(kUnknownBodyCapacity));
      return false;
    }

    const int available = stream->available();
    if (available > 0) {
      size_t wanted = static_cast<size_t>(available);
      if (wanted > 4096U) wanted = 4096U;
      const size_t remaining = capacity - 1U - body.size;
      if (wanted > remaining) wanted = remaining;
      if (wanted == 0U) return false;

      const int received = stream->read(body.data + body.size, wanted);
      if (received > 0) {
        body.size += static_cast<size_t>(received);
        lastDataMs = millis();
        if (body.size >= nextProgress) {
          DebugLog::printf("%s: received %u%s\n", providerLabel,
                           static_cast<unsigned>(body.size),
                           knownLength ? " B" : " B (unknown length)");
          nextProgress += 64U * 1024U;
        }
        delay(0);
        continue;
      }
    }

    const uint32_t now = millis();
    if (!http.connected() && stream->available() <= 0) break;
    if (static_cast<uint32_t>(now - lastDataMs) > kNoDataTimeoutMs) {
      DebugLog::printf("%s: body timeout after %u/%u B\n", providerLabel,
                       static_cast<unsigned>(body.size),
                       static_cast<unsigned>(expected));
      return false;
    }
    if (static_cast<uint32_t>(now - startedMs) > kTotalTimeoutMs) {
      DebugLog::printf("%s: body total timeout after %u/%u B\n", providerLabel,
                       static_cast<unsigned>(body.size),
                       static_cast<unsigned>(expected));
      return false;
    }
    delay(2);
  }

  body.data[body.size] = '\0';
  if (knownLength && body.size != expected) {
    DebugLog::printf("%s: truncated HTTP body: got %u / %u B\n", providerLabel,
                     static_cast<unsigned>(body.size),
                     static_cast<unsigned>(expected));
    return false;
  }

  DebugLog::printf("%s: body complete %u B in %u ms\n", providerLabel,
                   static_cast<unsigned>(body.size),
                   static_cast<unsigned>(millis() - startedMs));
  return body.size > 0U;
}

bool isInsideMap(float lat, float lon) {
  return lat <= Config::MAP_LAT_TOP && lat >= Config::MAP_LAT_BOTTOM &&
         lon >= Config::MAP_LON_LEFT && lon <= Config::MAP_LON_RIGHT;
}

bool elapsed(uint32_t now, uint32_t previous, uint32_t interval) {
  return previous == 0 ||
         static_cast<int32_t>(now - previous) >=
             static_cast<int32_t>(interval);
}

bool cacheFresh(const AircraftSnapshot& cache, uint32_t successMs,
                uint32_t nowMs, uint32_t maxAgeMs) {
  if (!cache.valid || successMs == 0) return false;
  return static_cast<uint32_t>(nowMs - successMs) <= maxAgeMs;
}

void resetSnapshot(AircraftSnapshot& snapshot) {
  snapshot.count = 0;
  snapshot.generated = 0;
  snapshot.valid = false;
  snapshot.localCount = 0;
  snapshot.adsbFiCount = 0;
  snapshot.mlatCount = 0;
  snapshot.endpoint[0] = '\0';
  snapshot.status[0] = '\0';
}

void addAircraftFilter(JsonDocument& filter, const char* arrayName) {
  JsonArray array = filter.createNestedArray(arrayName);
  JsonObject item = array.createNestedObject();
  item["hex"] = true;
  item["flight"] = true;
  item["r"] = true;
  item["t"] = true;
  item["type"] = true;
  item["lat"] = true;
  item["lon"] = true;
  item["track"] = true;
  item["gs"] = true;
  item["alt_baro"] = true;
  item["alt_geom"] = true;
  item["seen_pos"] = true;
  item["mlat"] = true;
}

bool isMlatPosition(JsonObjectConst source) {
  const char* sourceType = source["type"] | "";
  if (sourceType && strncasecmp(sourceType, "mlat", 4) == 0) return true;

  bool mlatLat = false;
  bool mlatLon = false;
  JsonArrayConst fields = source["mlat"].as<JsonArrayConst>();
  for (JsonVariantConst field : fields) {
    const char* name = field.as<const char*>();
    if (!name) continue;
    if (strcmp(name, "lat") == 0) mlatLat = true;
    if (strcmp(name, "lon") == 0) mlatLon = true;
  }
  return mlatLat && mlatLon;
}

int32_t readAltitude(JsonObjectConst source, bool& onGround) {
  onGround = false;
  JsonVariantConst altitude = source["alt_baro"];
  if (altitude.is<int32_t>() || altitude.is<float>() || altitude.is<double>()) {
    return altitude.as<int32_t>();
  }

  const char* text = altitude.as<const char*>();
  if (text && strcasecmp(text, "ground") == 0) {
    onGround = true;
    return 0;
  }

  // Geometric altitude is a useful fallback when barometric altitude is not
  // available. Keep the source distinction out of the renderer; the value is
  // used only for map colour/labeling.
  JsonVariantConst geometric = source["alt_geom"];
  if (geometric.is<int32_t>() || geometric.is<float>() ||
      geometric.is<double>()) {
    return geometric.as<int32_t>();
  }
  return -1;
}

struct ParseStats {
  size_t sourceCount = 0;
  size_t accepted = 0;
  size_t missingPosition = 0;
  size_t outsideMap = 0;
  size_t stalePosition = 0;
};

void parseAircraftArray(JsonVariantConst root, const char* arrayName,
                        bool fromLocal, const char* endpointLabel,
                        AircraftSnapshot& next, ParseStats* stats = nullptr) {
  uint64_t generated = root["now"] | static_cast<uint64_t>(0);
  if (generated > 100000000000ULL) generated /= 1000ULL;
  next.generated = static_cast<uint32_t>(generated);

  JsonArrayConst array = root[arrayName].as<JsonArrayConst>();
  if (stats) stats->sourceCount = array.size();
  for (JsonObjectConst source : array) {
    if (next.count >= Config::MAX_AIRCRAFT) break;
    if (source["lat"].isNull() || source["lon"].isNull()) {
      if (stats) ++stats->missingPosition;
      continue;
    }

    const float lat = source["lat"].as<float>();
    const float lon = source["lon"].as<float>();

    // adsb.fi v3 is a current snapshot. Some aircraft records may omit
    // seen_pos when the value is unavailable; do not discard an otherwise
    // valid fresh position just because that optional field is absent.
    float seen = 0.0f;
    if (!source["seen_pos"].isNull()) {
      seen = source["seen_pos"].as<float>();
      if (!isfinite(seen) || seen < 0.0f) seen = 0.0f;
    }

    if (!isfinite(lat) || !isfinite(lon)) {
      if (stats) ++stats->missingPosition;
      continue;
    }
    if (!isInsideMap(lat, lon)) {
      if (stats) ++stats->outsideMap;
      continue;
    }
    if (seen > Config::AIRCRAFT_MAX_AGE_SEC) {
      if (stats) ++stats->stalePosition;
      continue;
    }

    Aircraft& target = next.items[next.count++];
    strlcpy(target.hex, source["hex"] | "--------", sizeof(target.hex));

    String flight = source["flight"] | "";
    flight.trim();
    strlcpy(target.flight, flight.c_str(), sizeof(target.flight));
    strlcpy(target.registration, source["r"] | "", sizeof(target.registration));
    strlcpy(target.typeCode, source["t"] | "", sizeof(target.typeCode));

    target.lat = lat;
    target.lon = lon;
    target.trackDeg = source["track"] | 0.0f;
    target.groundSpeedKt = source["gs"] | 0.0f;
    target.seenPositionSec = seen;
    target.altitudeFt = readAltitude(source, target.onGround);
    target.mlatPosition = isMlatPosition(source);
    target.fromLocal = fromLocal;
    if (stats) ++stats->accepted;
  }

  next.valid = true;
  strlcpy(next.endpoint, endpointLabel, sizeof(next.endpoint));
}

int findAircraftByHex(const AircraftSnapshot& snapshot, const char* hex) {
  if (!hex || !hex[0] || strcmp(hex, "--------") == 0) return -1;
  for (size_t i = 0; i < snapshot.count; ++i) {
    if (strcasecmp(snapshot.items[i].hex, hex) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void enrichLocalAircraft(Aircraft& local, const Aircraft& network) {
  // Keep all local position/velocity values. adsb.fi is used only to fill
  // metadata that the local receiver may not know.
  if (!local.flight[0] && network.flight[0]) {
    strlcpy(local.flight, network.flight, sizeof(local.flight));
  }
  if (!local.registration[0] && network.registration[0]) {
    strlcpy(local.registration, network.registration, sizeof(local.registration));
  }
  if (!local.typeCode[0] && network.typeCode[0]) {
    strlcpy(local.typeCode, network.typeCode, sizeof(local.typeCode));
  }
  if (local.altitudeFt < 0 && network.altitudeFt >= 0) {
    local.altitudeFt = network.altitudeFt;
    local.onGround = network.onGround;
  }
}

}  // namespace

AdsbService::AdsbService(const char* aircraftUrl) : aircraftUrl_(aircraftUrl) {}

AdsbService::~AdsbService() {
  if (localCache_) {
    localCache_->~AircraftSnapshot();
    heap_caps_free(localCache_);
  }
  if (adsbFiCache_) {
    adsbFiCache_->~AircraftSnapshot();
    heap_caps_free(adsbFiCache_);
  }
}

bool AdsbService::ensureCaches() {
  if (localCache_ && adsbFiCache_) return true;

  if (!localCache_) {
    void* memory = heap_caps_malloc(sizeof(AircraftSnapshot),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory) localCache_ = new (memory) AircraftSnapshot();
  }
  if (!adsbFiCache_) {
    void* memory = heap_caps_malloc(sizeof(AircraftSnapshot),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory) adsbFiCache_ = new (memory) AircraftSnapshot();
  }

  if (!localCache_ || !adsbFiCache_) {
    snprintf(snapshot_.status, sizeof(snapshot_.status),
             "ADSB: malo PSRAM pro cache");
    return false;
  }
  return true;
}

uint32_t AdsbService::lastSuccessMs() const {
  if (!localEnabled_) return lastAdsbFiSuccessMs_;
  if (lastLocalSuccessMs_ == 0U) return lastAdsbFiSuccessMs_;
  if (lastAdsbFiSuccessMs_ == 0U) return lastLocalSuccessMs_;
  return static_cast<int32_t>(lastLocalSuccessMs_ - lastAdsbFiSuccessMs_) > 0
             ? lastLocalSuccessMs_
             : lastAdsbFiSuccessMs_;
}


void AdsbService::applyLocalSnapshot(const AircraftSnapshot& source) {
  if (!ensureCaches()) return;
  *localCache_ = source;
  localCache_->valid = source.valid;
  lastLocalSuccessMs_ = source.valid ? millis() : lastLocalSuccessMs_;
  consecutiveLocalFailures_ = 0;
  nextLocalAttemptMs_ = 0;
  mergeCaches(millis());
}

void AdsbService::applyNetworkSnapshot(const AircraftSnapshot& source) {
  if (!ensureCaches()) return;
  *adsbFiCache_ = source;
  adsbFiCache_->valid = source.valid;
  if (source.valid) lastAdsbFiSuccessMs_ = millis();

  if (strstr(source.endpoint, "adsb.lol")) {
    strlcpy(networkSource_, "adsb.lol", sizeof(networkSource_));
  } else {
    strlcpy(networkSource_, "adsb.fi", sizeof(networkSource_));
  }
  if (source.status[0]) {
    strlcpy(adsbFiStatus_, source.status, sizeof(adsbFiStatus_));
  }
  mergeCaches(millis());
}

void AdsbService::refreshMergedSnapshot() {
  if (!ensureCaches()) return;
  mergeCaches(millis());
}

bool AdsbService::update(bool includeNetwork) {
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(snapshot_.status, sizeof(snapshot_.status), "ADSB: WiFi offline");
    return snapshot_.valid;
  }

  const uint32_t nowMs = millis();
  if (!ensureCaches()) return snapshot_.valid;

  // Reuse the public snapshot as a temporary fetch buffer. AircraftSnapshot is
  // deliberately large (up to 180 aircraft), so it must never be allocated on
  // the Arduino loop-task stack. Successful fetches are copied into their
  // persistent caches, then snapshot_ is rebuilt by mergeCaches().
  const bool localAttemptDue =
      nextLocalAttemptMs_ == 0U ||
      static_cast<int32_t>(nowMs - nextLocalAttemptMs_) >= 0;
  if (localEnabled_ && localAttemptDue) {
    resetSnapshot(snapshot_);
    if (fetchLocal(snapshot_)) {
      *localCache_ = snapshot_;
      lastLocalSuccessMs_ = nowMs;
      if (consecutiveLocalFailures_ >= Config::ADSB_LOCAL_BACKOFF_AFTER_FAILURES) {
        DebugLog::println("ADSB local: receiver restored; normal 2 s polling resumed");
      }
      consecutiveLocalFailures_ = 0;
      nextLocalAttemptMs_ = 0;
    } else {
      if (consecutiveLocalFailures_ < 255U) ++consecutiveLocalFailures_;
      if (consecutiveLocalFailures_ >= Config::ADSB_LOCAL_BACKOFF_AFTER_FAILURES) {
        nextLocalAttemptMs_ = nowMs + Config::ADSB_LOCAL_FAILURE_BACKOFF_MS;
        DebugLog::printf(
            "ADSB local: %u consecutive failures; retry in %u s\n",
            static_cast<unsigned>(consecutiveLocalFailures_),
            static_cast<unsigned>(Config::ADSB_LOCAL_FAILURE_BACKOFF_MS / 1000UL));
      }
    }
  }

  if (includeNetwork &&
      elapsed(nowMs, lastAdsbFiAttemptMs_, Config::ADSB_FI_REFRESH_MS)) {
    lastAdsbFiAttemptMs_ = nowMs;
    resetSnapshot(snapshot_);
    if (fetchAdsbFi(snapshot_)) {
      *adsbFiCache_ = snapshot_;
      lastAdsbFiSuccessMs_ = nowMs;
    }
  }

  mergeCaches(nowMs);
  return snapshot_.valid;
}

bool AdsbService::fetchLocal(AircraftSnapshot& target) {
  if (aircraftUrl_.isEmpty()) {
    snprintf(target.status, sizeof(target.status), "local URL empty");
    return false;
  }

  HTTPClient http;
  // Keep the local readsb/dump1090 request simple and tolerant. HTTP/1.1 plus
  // Connection: close proved more reliable than forcing HTTP/1.0 on this ESP32.
  http.setConnectTimeout(3000);
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(aircraftUrl_)) {
    DebugLog::println("ADSB local: http.begin failed");
    return false;
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("Connection", "close");

  const int code = http.GET();
  lastNetworkHttpCode_ = code;
  const int contentLength = http.getSize();
  if (code != HTTP_CODE_OK) {
    const String errorText = HTTPClient::errorToString(code);
    DebugLog::printf("ADSB local: HTTP %d (%s), length=%d\n", code,
                     errorText.c_str(), contentLength);
    http.end();
    return false;
  }

  DebugLog::printf("ADSB local: HTTP 200, length=%d\n", contentLength);

  // Do not deserialize directly from the TCP stream. aircraft.json may arrive
  // in several network bursts and ArduinoJson can otherwise return
  // IncompleteInput while the local receiver is healthy. First read the full
  // HTTP body into PSRAM using the same robust path as adsb.fi.
  PsramHttpBody body;
  if (!downloadJsonBody(http, "ADSB local", contentLength, body)) {
    http.end();
    return false;
  }
  http.end();

  StaticJsonDocument<1024> filter;
  filter["now"] = true;
  addAircraftFilter(filter, "aircraft");

  // The filtered local document is normally small, but leave comfortable
  // PSRAM headroom for busy receivers without consuming internal heap.
  BasicJsonDocument<PsramAllocator> doc(160U * 1024U);
  const DeserializationError err = deserializeJson(
      doc, reinterpret_cast<char*>(body.data), body.size,
      DeserializationOption::Filter(filter));
  if (err) {
    DebugLog::printf("ADSB local JSON: %s | body=%u B\n", err.c_str(),
                     static_cast<unsigned>(body.size));
    return false;
  }

  parseAircraftArray(doc.as<JsonVariantConst>(), "aircraft", true,
                     "local aircraft.json", target);
  snprintf(target.status, sizeof(target.status), "local %u",
           static_cast<unsigned>(target.count));
  DebugLog::printf("ADSB local: parsed %u aircraft from %u B\n",
                   static_cast<unsigned>(target.count),
                   static_cast<unsigned>(body.size));
  return true;
}

bool AdsbService::fetchNetworkProvider(AircraftSnapshot& target,
                                       const char* providerLabel,
                                       const char* host,
                                       const char* url) {
  // Record the provider being attempted even if DNS/TLS fails. This is used
  // by the per-source web diagnostics.
  strlcpy(networkSource_, providerLabel, sizeof(networkSource_));
  lastNetworkHttpCode_ = 0;

  const uint32_t heapFree = ESP.getFreeHeap();
  const uint32_t heapLargest = static_cast<uint32_t>(
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  const uint32_t psramFree = ESP.getFreePsram();

  IPAddress resolved;
  const int dnsOk = WiFi.hostByName(host, resolved);
  if (dnsOk != 1) {
    lastNetworkHttpCode_ = -1001;
    snprintf(adsbFiStatus_, sizeof(adsbFiStatus_), "%s DNS chyba", providerLabel);
    DebugLog::printf("%s: DNS failed | heap=%u largest=%u psram=%u\n",
                     providerLabel, static_cast<unsigned>(heapFree),
                     static_cast<unsigned>(heapLargest),
                     static_cast<unsigned>(psramFree));
    return false;
  }

  DebugLog::printf(
      "%s GET: %s | DNS %s | heap=%u largest=%u psram=%u\n",
      providerLabel, url, resolved.toString().c_str(),
      static_cast<unsigned>(heapFree), static_cast<unsigned>(heapLargest),
      static_cast<unsigned>(psramFree));

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  HTTPClient http;
  http.setConnectTimeout(7000);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(client, url)) {
    lastNetworkHttpCode_ = -1002;
    snprintf(adsbFiStatus_, sizeof(adsbFiStatus_), "%s http.begin chyba",
             providerLabel);
    DebugLog::println(adsbFiStatus_);
    return false;
  }

  http.addHeader("User-Agent", "ESP32-Radar-ADSB/0.30.9");
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("Connection", "close");

  const int code = http.GET();
  lastNetworkHttpCode_ = code;
  const int contentLength = http.getSize();
  if (code != HTTP_CODE_OK) {
    const String errorText = HTTPClient::errorToString(code);
    snprintf(adsbFiStatus_, sizeof(adsbFiStatus_), "%s HTTP %d %.42s",
             providerLabel, code, errorText.c_str());
    DebugLog::printf(
        "%s: HTTP %d (%s), length=%d | heap=%u largest=%u\n",
        providerLabel, code, errorText.c_str(), contentLength,
        static_cast<unsigned>(ESP.getFreeHeap()),
        static_cast<unsigned>(heap_caps_get_largest_free_block(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    if (code > 0) {
      String detail = http.getString();
      detail.replace('\r', ' ');
      detail.replace('\n', ' ');
      if (detail.length() > 120) detail.remove(120);
      DebugLog::printf("%s error body: %s\n", providerLabel, detail.c_str());
    }
    releaseSecureHttp(http, client);
    return false;
  }

  DebugLog::printf("%s: HTTP 200, length=%d\n", providerLabel, contentLength);

  // Do not deserialize directly from WiFiClientSecure. On this target the TLS
  // stream can temporarily report no bytes while the response is still in
  // flight; ArduinoJson then reports IncompleteInput even though HTTP 200 and
  // Content-Length are valid. Buffer the complete body in PSRAM first and only
  // parse after all declared bytes arrived.
  PsramHttpBody body;
  if (!downloadJsonBody(http, providerLabel, contentLength, body)) {
    // HTTP itself succeeded, but the payload did not finish. Keep HTTP 200 in
    // diagnostics and expose the detailed status string separately.
    snprintf(adsbFiStatus_, sizeof(adsbFiStatus_), "%s body incomplete",
             providerLabel);
    releaseSecureHttp(http, client);
    return false;
  }
  releaseSecureHttp(http, client);

  StaticJsonDocument<1280> filter;
  filter["now"] = true;
  filter["total"] = true;
  filter["msg"] = true;
  addAircraftFilter(filter, "ac");

  BasicJsonDocument<PsramAllocator> doc(640U * 1024U);
  const DeserializationError err = deserializeJson(
      doc, reinterpret_cast<char*>(body.data), body.size,
      DeserializationOption::Filter(filter));
  if (err) {
    snprintf(adsbFiStatus_, sizeof(adsbFiStatus_), "%s JSON: %s",
             providerLabel, err.c_str());
    DebugLog::printf("%s JSON: %s | body=%u B psram free=%u\n", providerLabel,
                     err.c_str(), static_cast<unsigned>(body.size),
                     static_cast<unsigned>(ESP.getFreePsram()));
    return false;
  }

  const size_t apiTotal = doc["total"] | static_cast<size_t>(0);
  const char* apiMessage = doc["msg"] | "";
  const size_t apiArraySize = doc["ac"].as<JsonArrayConst>().size();

  ParseStats stats;
  parseAircraftArray(doc.as<JsonVariantConst>(), "ac", false, providerLabel,
                     target, &stats);

  strlcpy(networkSource_, providerLabel, sizeof(networkSource_));
  snprintf(adsbFiStatus_, sizeof(adsbFiStatus_), "%s OK: %u/%u v mape",
           providerLabel, static_cast<unsigned>(target.count),
           static_cast<unsigned>(apiArraySize));
  snprintf(target.status, sizeof(target.status), "%s", adsbFiStatus_);

  DebugLog::printf(
      "%s: total=%u ac=%u accepted=%u missing=%u outside=%u stale=%u msg=%s\n",
      providerLabel, static_cast<unsigned>(apiTotal),
      static_cast<unsigned>(stats.sourceCount),
      static_cast<unsigned>(stats.accepted),
      static_cast<unsigned>(stats.missingPosition),
      static_cast<unsigned>(stats.outsideMap),
      static_cast<unsigned>(stats.stalePosition), apiMessage);
  return true;
}

bool AdsbService::fetchAdsbFi(AircraftSnapshot& target) {
  char primaryUrl[192];
  snprintf(primaryUrl, sizeof(primaryUrl),
           "%s/v3/lat/%.4f/lon/%.4f/dist/%u",
           Config::ADSB_FI_BASE_URL, Config::ADSB_FI_CENTER_LAT,
           Config::ADSB_FI_CENTER_LON,
           static_cast<unsigned>(Config::ADSB_FI_RADIUS_NM));

  const uint32_t primaryStarted = millis();
  if (fetchNetworkProvider(target, "adsb.fi", "opendata.adsb.fi", primaryUrl)) {
    return true;
  }

  const uint32_t primaryDuration = millis() - primaryStarted;
  const int primaryCode = lastNetworkHttpCode_;
  // Do not launch a second TLS handshake after DNS/TLS/socket failures, low
  // memory symptoms (negative/internal codes), or an incomplete HTTP 200 body.
  // A fallback is useful only when adsb.fi itself returned a real HTTP error.
  // This prevents a failed handshake from being immediately followed by
  // another expensive handshake against adsb.lol.
  if (primaryCode < 400 || primaryCode >= 600 || primaryDuration >= 10000UL) {
    DebugLog::printf(
        "adsb.fi: fallback suppressed code=%d duration=%u ms\n",
        primaryCode, static_cast<unsigned>(primaryDuration));
    return false;
  }

  resetSnapshot(target);
  delay(Config::TLS_POST_REQUEST_SETTLE_MS);
  char fallbackUrl[192];
  snprintf(fallbackUrl, sizeof(fallbackUrl),
           "%s/v2/lat/%.4f/lon/%.4f/dist/%u",
           Config::ADSB_LOL_BASE_URL, Config::ADSB_FI_CENTER_LAT,
           Config::ADSB_FI_CENTER_LON,
           static_cast<unsigned>(Config::ADSB_FI_RADIUS_NM));
  return fetchNetworkProvider(target, "adsb.lol", "api.adsb.lol", fallbackUrl);
}

void AdsbService::mergeCaches(uint32_t nowMs) {
  const bool localFresh =
      localEnabled_ && localCache_ &&
      cacheFresh(*localCache_, lastLocalSuccessMs_, nowMs,
                 Config::ADSB_LOCAL_CACHE_MAX_AGE_MS);
  const bool networkFresh =
      adsbFiCache_ && cacheFresh(*adsbFiCache_, lastAdsbFiSuccessMs_, nowMs,
                 Config::ADSB_FI_CACHE_MAX_AGE_MS);

  resetSnapshot(snapshot_);
  snapshot_.valid = localFresh || networkFresh;
  snapshot_.generated = static_cast<uint32_t>(time(nullptr));
  snprintf(snapshot_.endpoint, sizeof(snapshot_.endpoint),
           localEnabled_ ? "local + %s" : "%s", networkSource_);

  if (localFresh) {
    for (size_t i = 0;
         i < localCache_->count && snapshot_.count < Config::MAX_AIRCRAFT; ++i) {
      snapshot_.items[snapshot_.count++] = localCache_->items[i];
      ++snapshot_.localCount;
    }
  }

  if (networkFresh) {
    for (size_t i = 0; i < adsbFiCache_->count; ++i) {
      const Aircraft& network = adsbFiCache_->items[i];
      const int duplicate = findAircraftByHex(snapshot_, network.hex);
      if (duplicate >= 0) {
        enrichLocalAircraft(
            snapshot_.items[static_cast<size_t>(duplicate)], network);
        continue;
      }
      if (snapshot_.count >= Config::MAX_AIRCRAFT) break;
      snapshot_.items[snapshot_.count++] = network;
      ++snapshot_.adsbFiCount;
    }
  }

  for (size_t i = 0; i < snapshot_.count; ++i) {
    if (snapshot_.items[i].mlatPosition) ++snapshot_.mlatCount;
  }

  if (snapshot_.valid) {
    if (localFresh && networkFresh) {
      snprintf(snapshot_.status, sizeof(snapshot_.status),
               "ADSB: %u | local %u + %s %u | MLAT %u",
               static_cast<unsigned>(snapshot_.count),
               static_cast<unsigned>(snapshot_.localCount), networkSource_,
               static_cast<unsigned>(snapshot_.adsbFiCount),
               static_cast<unsigned>(snapshot_.mlatCount));
    } else if (networkFresh) {
      snprintf(snapshot_.status, sizeof(snapshot_.status),
               "ADSB: %u | %s | MLAT %u",
               static_cast<unsigned>(snapshot_.count), networkSource_,
               static_cast<unsigned>(snapshot_.mlatCount));
    } else {
      snprintf(snapshot_.status, sizeof(snapshot_.status),
               "ADSB: %u | local | %.70s",
               static_cast<unsigned>(snapshot_.count), adsbFiStatus_);
    }
  } else {
    snprintf(snapshot_.status, sizeof(snapshot_.status),
             "ADSB: bez dat | %.80s", adsbFiStatus_);
  }
}
