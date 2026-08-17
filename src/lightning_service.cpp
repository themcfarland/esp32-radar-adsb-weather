#include "lightning_service.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>


namespace {
constexpr char kServer[] = "live2.lightningmaps.org";
constexpr uint16_t kWebSocketPort = 443;
constexpr char kWebSocketPath[] = "/";
constexpr uint32_t kReconnectDelayMs = 5000;
constexpr float kViewportMarginDeg = 0.35f;

float mercatorY(float latitudeDeg) {
  const float latitude = constrain(latitudeDeg, -85.0f, 85.0f) * DEG_TO_RAD;
  return logf(tanf(PI * 0.25f + latitude * 0.5f));
}

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8U) << 8) |
                               ((g & 0xFCU) << 3) |
                               (b >> 3));
}

float greatCircleDistanceKm(float lat1Deg, float lon1Deg, float lat2Deg,
                            float lon2Deg) {
  constexpr float kEarthRadiusKm = 6371.0088f;
  const float lat1 = lat1Deg * DEG_TO_RAD;
  const float lat2 = lat2Deg * DEG_TO_RAD;
  const float dLat = (lat2Deg - lat1Deg) * DEG_TO_RAD;
  const float dLon = (lon2Deg - lon1Deg) * DEG_TO_RAD;
  const float sinHalfLat = sinf(dLat * 0.5f);
  const float sinHalfLon = sinf(dLon * 0.5f);
  const float a = sinHalfLat * sinHalfLat +
                  cosf(lat1) * cosf(lat2) * sinHalfLon * sinHalfLon;
  const float clamped = constrain(a, 0.0f, 1.0f);
  return 2.0f * kEarthRadiusKm *
         atan2f(sqrtf(clamped), sqrtf(1.0f - clamped));
}
}  // namespace

LightningService::LightningService() = default;

LightningService::~LightningService() {
  webSocket_.disconnect();
  if (strikes_) heap_caps_free(strikes_);
  delete jsonDoc_;
}

bool LightningService::begin() {
  strikes_ = static_cast<Strike*>(heap_caps_calloc(
      kMaxStrikes, sizeof(Strike), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  jsonDoc_ = new DynamicJsonDocument(kJsonCapacity);

  if (!strikes_ || !jsonDoc_) {
    snprintf(status_, sizeof(status_), "Blesky: malo RAM/PSRAM pro LightningMaps");
    return false;
  }

  webSocket_.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
    onWebSocketEvent(type, payload, length);
  });
  webSocket_.setReconnectInterval(60000);  // service handles reconnect itself
  webSocket_.enableHeartbeat(15000, 3000, 2);
  snprintf(status_, sizeof(status_), "Blesky: LightningMaps JSON pripraven");
  return true;
}

String LightningService::buildSubscription() const {
  const float north = min(85.0f, Config::MAP_LAT_TOP + kViewportMarginDeg);
  const float east = min(180.0f, Config::MAP_LON_RIGHT + kViewportMarginDeg);
  const float south = max(-85.0f, Config::MAP_LAT_BOTTOM - kViewportMarginDeg);
  const float west = max(-180.0f, Config::MAP_LON_LEFT - kViewportMarginDeg);

  char json[320];
  snprintf(json, sizeof(json),
           "{\"v\":24,\"i\":{},\"s\":false,\"x\":0,\"w\":0,"
           "\"tx\":0,\"tw\":1,\"a\":4,\"z\":6,\"b\":true,\"h\":\"\","
           "\"l\":1,\"t\":1,\"from_lightningmaps_org\":true,"
           "\"p\":[%.2f,%.2f,%.2f,%.2f],\"r\":\"A\"}",
           north, east, south, west);
  return String(json);
}

void LightningService::connectServer() {
  if (WiFi.status() != WL_CONNECTED || socketStarted_) return;
  snprintf(status_, sizeof(status_), "Blesky: pripojuji %s", kServer);
  Serial.printf("Lightning: connecting wss://%s%s\n", kServer, kWebSocketPath);

  // Use the same origin as the browser map. No API key or WebSocket
  // subprotocol is required by the currently observed live2 endpoint.
  webSocket_.setExtraHeaders("Origin: https://www.lightningmaps.org");
  webSocket_.beginSSL(kServer, kWebSocketPort, kWebSocketPath, nullptr, "");
  socketStarted_ = true;
}

void LightningService::forceReconnect(const char* reason) {
  Serial.printf("Lightning watchdog: %s on %s -> reconnect\n",
                reason ? reason : "stale JSON feed", kServer);

  forcedDisconnect_ = true;
  webSocket_.disconnect();
  connected_ = false;
  socketStarted_ = false;
  connectedAtMs_ = 0;
  lastValidFrameMs_ = 0;
  reconnectAtMs_ = millis() + Config::LIGHTNING_WATCHDOG_RECONNECT_DELAY_MS;
  ++watchdogReconnects_;
  snprintf(status_, sizeof(status_), "Blesky: JSON watchdog reconnect (%u)",
           static_cast<unsigned>(watchdogReconnects_));
}

bool LightningService::loop(bool enabled) {
  dataChanged_ = false;

  if (!enabled || WiFi.status() != WL_CONNECTED) {
    if (socketStarted_) {
      webSocket_.disconnect();
      socketStarted_ = false;
      connected_ = false;
    }
    snprintf(status_, sizeof(status_), enabled ? "Blesky: WiFi offline"
                                               : "Blesky: vrstva vypnuta");
    return false;
  }

  if (!socketStarted_ && static_cast<int32_t>(millis() - reconnectAtMs_) >= 0) {
    connectServer();
  }

  if (socketStarted_) webSocket_.loop();

  // Ping/pong detects a dead TCP/WSS link. This second guard detects a socket
  // that stays connected but stops delivering valid LightningMaps JSON. A
  // valid envelope counts even when strokes[] is empty, so quiet weather does
  // not depend on having a local strike.
  if (connected_ && socketStarted_) {
    const uint32_t nowMs = millis();
    if (lastValidFrameMs_ == 0U) {
      if (connectedAtMs_ != 0U &&
          nowMs - connectedAtMs_ >= Config::LIGHTNING_FIRST_DATA_TIMEOUT_MS) {
        forceReconnect("no first JSON frame");
      }
    } else if (nowMs - lastValidFrameMs_ >=
               Config::LIGHTNING_STALE_DATA_TIMEOUT_MS) {
      forceReconnect("no valid JSON data");
    }
  }

  const time_t now = time(nullptr);
  if (now > 1700000000) pruneOldStrikes(static_cast<uint32_t>(now));

  if (strikeCount_ > 0 &&
      millis() - lastAgeRedrawMs_ >= Config::LIGHTNING_REDRAW_MS) {
    lastAgeRedrawMs_ = millis();
    dataChanged_ = true;
  }
  return dataChanged_;
}

void LightningService::onWebSocketEvent(WStype_t type, uint8_t* payload,
                                        size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      connected_ = true;
      connectedAtMs_ = millis();
      lastValidFrameMs_ = 0;
      String subscription = buildSubscription();
      webSocket_.sendTXT(subscription);
      snprintf(status_, sizeof(status_), "Blesky: LightningMaps LIVE, %u bodu",
               static_cast<unsigned>(strikeCount_));
      Serial.printf("Lightning: connected to %s\n", kServer);
      Serial.printf("Lightning: subscribe %s\n", subscription.c_str());
      break;
    }

    case WStype_DISCONNECTED:
    case WStype_ERROR:
      if (forcedDisconnect_) {
        forcedDisconnect_ = false;
        break;
      }
      if (socketStarted_ || connected_) {
        Serial.printf("Lightning: WebSocket disconnected/error on %s\n", kServer);
      }
      connected_ = false;
      socketStarted_ = false;
      connectedAtMs_ = 0;
      lastValidFrameMs_ = 0;
      reconnectAtMs_ = millis() + kReconnectDelayMs;
      snprintf(status_, sizeof(status_), "Blesky: WSS odpojen, reconnect");
      break;

    case WStype_TEXT:
      if (payload && length > 0) handleJsonMessage(payload, length);
      break;

    default:
      break;
  }
}

bool LightningService::handleJsonMessage(const uint8_t* payload,
                                         size_t length) {
  if (!payload || length == 0U) return false;

  // Parse only the fields needed by the ESP32. This keeps the ArduinoJson DOM
  // small even when the server batches many strokes in one WebSocket frame.
  StaticJsonDocument<256> filter;
  filter["time"] = true;
  filter["strokes"][0]["time"] = true;
  filter["strokes"][0]["lat"] = true;
  filter["strokes"][0]["lon"] = true;
  filter["strokes"][0]["id"] = true;

  if (!jsonDoc_) return false;
  jsonDoc_->clear();
  const DeserializationError error = deserializeJson(
      *jsonDoc_, payload, length, DeserializationOption::Filter(filter));
  if (error) {
    ++jsonErrors_;
    if (jsonErrors_ <= 5U || (jsonErrors_ & 0x3FU) == 1U) {
      Serial.printf("Lightning: JSON parse failed (%s), bytes=%u, errors=%u\n",
                    error.c_str(), static_cast<unsigned>(length),
                    static_cast<unsigned>(jsonErrors_));
    }
    return false;
  }

  if (!(*jsonDoc_)["time"].is<uint32_t>() ||
      !(*jsonDoc_)["strokes"].is<JsonArray>()) {
    ++jsonErrors_;
    return false;
  }

  ++jsonMessages_;
  lastSuccessMs_ = millis();
  lastValidFrameMs_ = lastSuccessMs_;

  JsonArray strokes = (*jsonDoc_)["strokes"].as<JsonArray>();
  size_t accepted = 0;
  for (JsonObject stroke : strokes) {
    const uint64_t timeMs = stroke["time"] | 0ULL;
    const float lat = stroke["lat"] | NAN;
    const float lon = stroke["lon"] | NAN;
    const uint32_t id = stroke["id"] | 0U;

    if (timeMs < 1700000000000ULL || !isfinite(lat) || !isfinite(lon) ||
        fabsf(lat) > 90.0f || fabsf(lon) > 180.0f) {
      continue;
    }

    const uint32_t epochSec = static_cast<uint32_t>(timeMs / 1000ULL);
    ++strokesReceived_;
    if (strokesReceived_ <= 5U) {
      Serial.printf("Lightning JSON #%u: id=%u t=%u lat=%.6f lon=%.6f\n",
                    static_cast<unsigned>(strokesReceived_),
                    static_cast<unsigned>(id),
                    static_cast<unsigned>(epochSec), lat, lon);
    }
    if (addStrike(epochSec, lat, lon, id)) {
      ++accepted;
      dataChanged_ = true;
    }
  }

  if (jsonMessages_ <= 3U || accepted > 0U || (jsonMessages_ & 0x7FU) == 1U) {
    snprintf(status_, sizeof(status_),
             "Blesky: LightningMaps LIVE, %u bodu",
             static_cast<unsigned>(strikeCount_));
  }
  return true;
}

bool LightningService::addStrike(uint32_t epochSec, float lat, float lon,
                                 uint32_t id) {
  // Keep a local guard even though LightningMaps already filters the feed by
  // viewport. This protects the buffer if the remote subscription semantics
  // ever change.
  constexpr float margin = 0.35f;
  if (lat < Config::MAP_LAT_BOTTOM - margin || lat > Config::MAP_LAT_TOP + margin ||
      lon < Config::MAP_LON_LEFT - margin || lon > Config::MAP_LON_RIGHT + margin) {
    return false;
  }

  // LightningMaps provides a stable stroke id. Prefer it for duplicate
  // suppression after reconnects/replayed batches; retain a coordinate/time
  // fallback in case an id is ever missing.
  const size_t check = min(strikeCount_, static_cast<size_t>(32));
  for (size_t i = 0; i < check; ++i) {
    const size_t idx = (strikeWrite_ + kMaxStrikes - 1 - i) % kMaxStrikes;
    const Strike& old = strikes_[idx];
    if (id != 0U && old.id == id) return false;
    if (old.epochSec == epochSec && fabsf(old.lat - lat) < 0.00001f &&
        fabsf(old.lon - lon) < 0.00001f) {
      return false;
    }
  }

  strikes_[strikeWrite_] = {lat, lon, epochSec, id};
  strikeWrite_ = (strikeWrite_ + 1) % kMaxStrikes;
  if (strikeCount_ < kMaxStrikes) ++strikeCount_;
  return true;
}

void LightningService::pruneOldStrikes(uint32_t nowEpoch) {
  if (!strikes_ || strikeCount_ == 0) return;
  const uint32_t cutoff = nowEpoch > kHistorySeconds ? nowEpoch - kHistorySeconds : 0;
  for (size_t i = 0; i < kMaxStrikes; ++i) {
    if (strikes_[i].epochSec != 0 && strikes_[i].epochSec < cutoff) {
      strikes_[i].epochSec = 0;
      if (strikeCount_ > 0) --strikeCount_;
    }
  }
}

bool LightningService::recentStrikeWithin(float centerLat, float centerLon,
                                          float radiusKm,
                                          uint32_t maxAgeSec) const {
  if (!strikes_ || strikeCount_ == 0 || radiusKm <= 0.0f ||
      !isfinite(centerLat) || !isfinite(centerLon)) {
    return false;
  }

  const time_t nowTime = time(nullptr);
  if (nowTime <= 1700000000) return false;
  const uint32_t nowEpoch = static_cast<uint32_t>(nowTime);

  for (size_t i = 0; i < kMaxStrikes; ++i) {
    const Strike& strike = strikes_[i];
    if (strike.epochSec == 0 || strike.epochSec > nowEpoch + 5U) continue;
    if (nowEpoch - strike.epochSec > maxAgeSec) continue;
    if (greatCircleDistanceKm(centerLat, centerLon, strike.lat, strike.lon) <=
        radiusKm) {
      return true;
    }
  }
  return false;
}

bool LightningService::ready() const {
  return strikes_ != nullptr;
}

int LightningService::mapX(float lon, uint16_t width,
                           const MapViewport& viewport) const {
  if (viewport.lonRight <= viewport.lonLeft || width < 2) return -1;
  return static_cast<int>(lroundf((lon - viewport.lonLeft) /
                                  (viewport.lonRight - viewport.lonLeft) *
                                  (width - 1)));
}

int LightningService::mapY(float lat, uint16_t height,
                           const MapViewport& viewport) const {
  if (height < 2) return -1;
  const float top = mercatorY(viewport.latTop);
  const float bottom = mercatorY(viewport.latBottom);
  const float y = mercatorY(lat);
  if (top == bottom) return -1;
  return static_cast<int>(lroundf((top - y) / (top - bottom) * (height - 1)));
}

void LightningService::drawStrike(uint16_t* destination, uint16_t width,
                                  uint16_t height, int x, int y,
                                  uint16_t color, uint32_t ageSec) const {
  if (!destination || width == 0 || height == 0) return;

  auto put = [&](int px, int py, uint16_t c) {
    if (px < 0 || py < 0 || px >= static_cast<int>(width) ||
        py >= static_cast<int>(height)) return;
    destination[static_cast<size_t>(py) * width + px] = c;
  };

  auto line = [&](int x0, int y0, int x1, int y1, uint16_t c) {
    const int dx = abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
      put(x0, y0, c);
      if (x0 == x1 && y0 == y1) break;
      const int e2 = err * 2;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  };

  const uint16_t shadow = rgb565(5, 8, 12);

  if (ageSec <= Config::LIGHTNING_TRAIL_WHITE_MAX_AGE_SEC) {
    // Only the newest 0-2 minute strike uses a recognisable compact bolt.
    // Keep it just 9 px high so dense cells cannot merge into vertical bars.
    line(x + 2, y - 4, x - 1, y - 1, shadow);
    line(x - 1, y - 1, x + 1, y - 1, shadow);
    line(x + 1, y - 1, x - 2, y + 4, shadow);
    line(x + 1, y - 4, x - 2, y - 1, color);
    line(x - 2, y - 1, x, y - 1, color);
    line(x, y - 1, x - 3, y + 4, color);
    put(x, y, color);  // exact strike coordinate
    return;
  }

  // Older trail entries are deliberately point-like. The previous 13 px bolt
  // repeated hundreds of times made real N-S storm lines look like artificial
  // vertical dashed columns. A centred cross/diamond shows the exact lat/lon
  // without implying a direction. Size fades with age; colour still carries
  // the requested 2-5 / 5-10 / 10-20 minute trail information.
  int radius = 1;
  if (ageSec <= Config::LIGHTNING_TRAIL_YELLOW_MAX_AGE_SEC) radius = 2;

  put(x + 1, y + 1, shadow);
  put(x, y, color);
  for (int d = 1; d <= radius; ++d) {
    put(x - d, y, color);
    put(x + d, y, color);
    put(x, y - d, color);
    put(x, y + d, color);
  }
  if (radius >= 2) {
    put(x - 1, y - 1, color);
    put(x + 1, y - 1, color);
    put(x - 1, y + 1, color);
    put(x + 1, y + 1, color);
  }
}

uint16_t LightningService::trailColorForAge(uint32_t ageSec) const {
  if (ageSec <= Config::LIGHTNING_TRAIL_WHITE_MAX_AGE_SEC) {
    return rgb565(255, 255, 255);
  }
  if (ageSec <= Config::LIGHTNING_TRAIL_YELLOW_MAX_AGE_SEC) {
    return rgb565(255, 224, 0);
  }
  if (ageSec <= Config::LIGHTNING_TRAIL_ORANGE_MAX_AGE_SEC) {
    return rgb565(255, 128, 0);
  }
  if (ageSec <= Config::LIGHTNING_TRAIL_RED_MAX_AGE_SEC) {
    return rgb565(255, 40, 40);
  }
  return 0;
}

bool LightningService::renderLive(uint16_t* destination, uint16_t width,
                                  uint16_t height,
                                  const MapViewport& viewport) const {
  if (!destination || !ready() || !strikes_) return false;

  const time_t nowTime = time(nullptr);
  if (nowTime <= 1700000000) return false;
  const uint32_t nowEpoch = static_cast<uint32_t>(nowTime);

  size_t rendered = 0;

  // Independent realtime overlay: every strike is evaluated only against the
  // real current time. Radar frame changes do not select, hide or recolour it.
  // Older bands are rendered first so a newer strike wins where icons overlap.
  for (int band = 3; band >= 0; --band) {
    for (size_t i = 0; i < kMaxStrikes; ++i) {
      const Strike& strike = strikes_[i];
      if (strike.epochSec == 0 || strike.epochSec > nowEpoch + 5U) continue;

      const uint32_t ageSec = nowEpoch - strike.epochSec;
      if (ageSec > Config::LIGHTNING_TRAIL_RED_MAX_AGE_SEC) continue;

      int strikeBand = 0;
      if (ageSec > Config::LIGHTNING_TRAIL_ORANGE_MAX_AGE_SEC) {
        strikeBand = 3;  // 10-20 min: red
      } else if (ageSec > Config::LIGHTNING_TRAIL_YELLOW_MAX_AGE_SEC) {
        strikeBand = 2;  // 5-10 min: orange
      } else if (ageSec > Config::LIGHTNING_TRAIL_WHITE_MAX_AGE_SEC) {
        strikeBand = 1;  // 2-5 min: yellow
      }
      if (strikeBand != band) continue;

      if (strike.lon < viewport.lonLeft || strike.lon > viewport.lonRight ||
          strike.lat < viewport.latBottom || strike.lat > viewport.latTop) {
        continue;
      }

      const uint16_t color = trailColorForAge(ageSec);
      if (color == 0) continue;
      const int x = mapX(strike.lon, width, viewport);
      const int y = mapY(strike.lat, height, viewport);
      drawStrike(destination, width, height, x, y, color, ageSec);
      ++rendered;
    }
  }
  return rendered > 0;
}
