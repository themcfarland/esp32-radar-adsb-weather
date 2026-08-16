#include "lightning_service.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>


namespace {
constexpr const char* kServers[] = {
    "ws7.blitzortung.org",
    "ws1.blitzortung.org",
    "ws8.blitzortung.org",
};
constexpr size_t kServerCount = sizeof(kServers) / sizeof(kServers[0]);
constexpr uint16_t kWebSocketPort = 443;
constexpr char kWebSocketPath[] = "/";
constexpr char kSubscription[] = "{\"a\":111}";
constexpr uint32_t kReconnectDelayMs = 5000;

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
  if (lzwPrefix_) heap_caps_free(lzwPrefix_);
  if (lzwSuffix_) heap_caps_free(lzwSuffix_);
  if (lzwFirst_) heap_caps_free(lzwFirst_);
  if (lzwStack_) heap_caps_free(lzwStack_);
}

bool LightningService::begin() {
  strikes_ = static_cast<Strike*>(heap_caps_calloc(
      kMaxStrikes, sizeof(Strike), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  lzwPrefix_ = static_cast<uint16_t*>(heap_caps_malloc(
      kLzwDictionarySize * sizeof(uint16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  lzwSuffix_ = static_cast<uint8_t*>(heap_caps_malloc(
      kLzwDictionarySize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  lzwFirst_ = static_cast<uint8_t*>(heap_caps_malloc(
      kLzwDictionarySize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  lzwStack_ = static_cast<uint8_t*>(heap_caps_malloc(
      kLzwDictionarySize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

  if (!strikes_ || !lzwPrefix_ || !lzwSuffix_ || !lzwFirst_ || !lzwStack_) {
    snprintf(status_, sizeof(status_), "Blesky: malo PSRAM pro Blitzortung");
    return false;
  }

  webSocket_.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
    onWebSocketEvent(type, payload, length);
  });
  webSocket_.setReconnectInterval(60000);  // service rotates servers itself
  webSocket_.enableHeartbeat(15000, 3000, 2);
  snprintf(status_, sizeof(status_), "Blesky: Blitzortung pripraven");
  return true;
}

void LightningService::connectCurrentServer() {
  if (WiFi.status() != WL_CONNECTED || socketStarted_) return;
  const char* host = kServers[serverIndex_ % kServerCount];
  snprintf(status_, sizeof(status_), "Blesky: pripojuji %s", host);
  Serial.printf("Lightning: connecting wss://%s%s\n", host, kWebSocketPath);

  // Match a browser-style connection: no WebSocket subprotocol, Blitzortung origin.
  // A null fingerprint makes arduinoWebSockets use WiFiClientSecure::setInsecure().
  webSocket_.setExtraHeaders("Origin: https://www.blitzortung.org");
  webSocket_.beginSSL(host, kWebSocketPort, kWebSocketPath, nullptr, "");
  socketStarted_ = true;
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
    connectCurrentServer();
  }

  if (socketStarted_) webSocket_.loop();

  const time_t now = time(nullptr);
  if (now > 1700000000) pruneOldStrikes(static_cast<uint32_t>(now));

  // Even with a paused radar and no newly received local strike, refresh the
  // map periodically so white/yellow/orange/red age bands advance and strikes
  // disappear after 20 minutes. This is independent of radarFrame.
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
    case WStype_CONNECTED:
      connected_ = true;
      webSocket_.sendTXT(kSubscription);
      snprintf(status_, sizeof(status_), "Blesky: Blitzortung LIVE %s, %u bodu",
               kServers[serverIndex_ % kServerCount],
               static_cast<unsigned>(strikeCount_));
      Serial.printf("Lightning: connected to %s, subscription %s sent\n",
                    kServers[serverIndex_ % kServerCount], kSubscription);
      break;

    case WStype_DISCONNECTED:
    case WStype_ERROR:
      if (socketStarted_ || connected_) {
        Serial.printf("Lightning: WebSocket disconnected/error on %s\n",
                      kServers[serverIndex_ % kServerCount]);
      }
      connected_ = false;
      socketStarted_ = false;
      serverIndex_ = static_cast<uint8_t>((serverIndex_ + 1) % kServerCount);
      reconnectAtMs_ = millis() + kReconnectDelayMs;
      snprintf(status_, sizeof(status_), "Blesky: WSS odpojen, dalsi %s",
               kServers[serverIndex_]);
      break;

    case WStype_TEXT:
      if (payload && length > 0) handleCompressedMessage(payload, length);
      break;

    default:
      break;
  }
}

bool LightningService::decodeUtf8Code(const uint8_t* payload, size_t length,
                                      size_t& offset, uint16_t& code) const {
  if (!payload || offset >= length) return false;
  const uint8_t b0 = payload[offset++];
  if (b0 < 0x80U) {
    code = b0;
    return true;
  }

  if ((b0 & 0xE0U) == 0xC0U && offset < length) {
    const uint8_t b1 = payload[offset++];
    if ((b1 & 0xC0U) != 0x80U) return false;
    code = static_cast<uint16_t>(((b0 & 0x1FU) << 6) | (b1 & 0x3FU));
    return true;
  }

  if ((b0 & 0xF0U) == 0xE0U && offset + 1 < length) {
    const uint8_t b1 = payload[offset++];
    const uint8_t b2 = payload[offset++];
    if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U) return false;
    code = static_cast<uint16_t>(((b0 & 0x0FU) << 12) |
                                 ((b1 & 0x3FU) << 6) | (b2 & 0x3FU));
    return true;
  }

  // Blitzortung's browser LZW decoder uses JavaScript charCodeAt(), therefore
  // useful dictionary symbols are 16-bit code units. Four-byte UTF-8 is not
  // expected here and is rejected instead of silently corrupting the stream.
  return false;
}

bool LightningService::appendDictionaryEntry(uint16_t code, uint16_t nextCode,
                                              String& output,
                                              uint8_t& firstChar) {
  if (code < 256U) {
    firstChar = static_cast<uint8_t>(code);
    output += static_cast<char>(firstChar);
    return true;
  }
  if (code >= nextCode || code >= kLzwDictionarySize) return false;

  size_t depth = 0;
  uint16_t cursor = code;
  while (cursor >= 256U) {
    if (cursor >= nextCode || cursor >= kLzwDictionarySize ||
        depth >= kLzwDictionarySize - 1) {
      return false;
    }
    lzwStack_[depth++] = lzwSuffix_[cursor];
    cursor = lzwPrefix_[cursor];
  }
  lzwStack_[depth++] = static_cast<uint8_t>(cursor);
  firstChar = lzwStack_[depth - 1];
  while (depth > 0) output += static_cast<char>(lzwStack_[--depth]);
  return true;
}

bool LightningService::decodeHeaderLzw(const uint8_t* payload, size_t length,
                                       String& decoded) {
  decoded = "";
  decoded.reserve(384);
  if (!payload || length == 0) return false;

  size_t offset = 0;
  uint16_t firstCode = 0;
  if (!decodeUtf8Code(payload, length, offset, firstCode) || firstCode >= 256U) {
    return false;
  }

  decoded += static_cast<char>(firstCode);
  uint16_t previousCode = firstCode;
  uint8_t previousFirst = static_cast<uint8_t>(firstCode);
  uint16_t nextCode = 256U;

  while (offset < length && decoded.length() < 768U) {
    uint16_t code = 0;
    if (!decodeUtf8Code(payload, length, offset, code)) return false;
    if (code > nextCode || nextCode >= kLzwDictionarySize) return false;

    uint8_t currentFirst = 0;
    if (code < 256U) {
      currentFirst = static_cast<uint8_t>(code);
    } else if (code < nextCode) {
      currentFirst = lzwFirst_[code];
    } else {  // KwKwK case: current entry is previous + first(previous)
      currentFirst = previousFirst;
    }

    lzwPrefix_[nextCode] = previousCode;
    lzwSuffix_[nextCode] = currentFirst;
    lzwFirst_[nextCode] = previousFirst;
    const uint16_t insertedCode = nextCode;
    ++nextCode;

    uint8_t emittedFirst = 0;
    const uint16_t emitCode = (code == insertedCode) ? insertedCode : code;
    if (!appendDictionaryEntry(emitCode, nextCode, decoded, emittedFirst)) {
      return false;
    }

    previousCode = code;
    previousFirst = currentFirst;

    uint64_t timeNs = 0;
    float lat = 0.0f;
    float lon = 0.0f;
    if (decoded.length() > 45U && extractStrikeHeader(decoded, timeNs, lat, lon)) {
      return true;
    }
  }

  uint64_t timeNs = 0;
  float lat = 0.0f;
  float lon = 0.0f;
  return extractStrikeHeader(decoded, timeNs, lat, lon);
}

bool LightningService::parseUnsignedField(const String& text, const char* key,
                                          uint64_t& value) const {
  const int keyPos = text.indexOf(key);
  if (keyPos < 0) return false;
  int p = text.indexOf(':', keyPos + strlen(key));
  if (p < 0) return false;
  ++p;
  while (p < static_cast<int>(text.length()) && text[p] == ' ') ++p;
  const int start = p;
  while (p < static_cast<int>(text.length()) && text[p] >= '0' && text[p] <= '9') ++p;
  if (p == start) return false;
  char number[32];
  const size_t n = min(static_cast<size_t>(p - start), sizeof(number) - 1);
  memcpy(number, text.c_str() + start, n);
  number[n] = '\0';
  value = strtoull(number, nullptr, 10);
  return value > 0;
}

bool LightningService::parseFloatField(const String& text, const char* key,
                                       float& value) const {
  const int keyPos = text.indexOf(key);
  if (keyPos < 0) return false;
  int p = text.indexOf(':', keyPos + strlen(key));
  if (p < 0) return false;
  ++p;
  while (p < static_cast<int>(text.length()) && text[p] == ' ') ++p;
  const int start = p;
  if (p < static_cast<int>(text.length()) && (text[p] == '-' || text[p] == '+')) ++p;
  while (p < static_cast<int>(text.length()) &&
         ((text[p] >= '0' && text[p] <= '9') || text[p] == '.' ||
          text[p] == 'e' || text[p] == 'E' || text[p] == '-' || text[p] == '+')) {
    ++p;
  }
  if (p == start) return false;
  char number[32];
  const size_t n = min(static_cast<size_t>(p - start), sizeof(number) - 1);
  memcpy(number, text.c_str() + start, n);
  number[n] = '\0';
  value = strtof(number, nullptr);
  return isfinite(value);
}

bool LightningService::extractStrikeHeader(const String& decoded,
                                            uint64_t& timeNs, float& lat,
                                            float& lon) const {
  return parseUnsignedField(decoded, "\"time\"", timeNs) &&
         parseFloatField(decoded, "\"lat\"", lat) &&
         parseFloatField(decoded, "\"lon\"", lon);
}

bool LightningService::handleCompressedMessage(const uint8_t* payload,
                                               size_t length) {
  String header;
  if (!decodeHeaderLzw(payload, length, header)) {
    ++rejectedMessages_;
    if (rejectedMessages_ <= 3U || (rejectedMessages_ & 0x3FU) == 1U) {
      Serial.printf("Lightning: LZW/header decode failed, compressed=%u bytes, rejected=%u\n",
                    static_cast<unsigned>(length),
                    static_cast<unsigned>(rejectedMessages_));
      if (!header.isEmpty()) {
        String preview = header.substring(0, min(static_cast<unsigned>(header.length()), 180U));
        Serial.printf("Lightning decoded preview: %s\n", preview.c_str());
      }
    }
    return false;
  }

  uint64_t timeNs = 0;
  float lat = 0.0f;
  float lon = 0.0f;
  if (!extractStrikeHeader(header, timeNs, lat, lon)) {
    ++rejectedMessages_;
    return false;
  }

  ++decodedMessages_;
  lastSuccessMs_ = millis();
  const uint32_t epochSec = static_cast<uint32_t>(timeNs / 1000000000ULL);
  if (decodedMessages_ <= 3U) {
    Serial.printf("Lightning decoded #%u: t=%u lat=%.6f lon=%.6f\n",
                  static_cast<unsigned>(decodedMessages_),
                  static_cast<unsigned>(epochSec), lat, lon);
  }
  if (epochSec < 1700000000UL || fabsf(lat) > 90.0f || fabsf(lon) > 180.0f) {
    ++rejectedMessages_;
    return false;
  }

  const bool accepted = addStrike(epochSec, lat, lon);
  if (accepted) dataChanged_ = true;

  if ((decodedMessages_ & 0x7FU) == 1U || accepted) {
    snprintf(status_, sizeof(status_),
             "Blesky: Blitzortung LIVE %s, %u bodu",
             kServers[serverIndex_ % kServerCount],
             static_cast<unsigned>(strikeCount_));
  }
  return true;
}

bool LightningService::addStrike(uint32_t epochSec, float lat, float lon) {
  // Keep only strikes that can become visible in the fixed Czech map plus a
  // small edge margin. This avoids storing the global Blitzortung firehose.
  constexpr float margin = 0.35f;
  if (lat < Config::MAP_LAT_BOTTOM - margin || lat > Config::MAP_LAT_TOP + margin ||
      lon < Config::MAP_LON_LEFT - margin || lon > Config::MAP_LON_RIGHT + margin) {
    return false;
  }

  // Cheap duplicate guard for reconnects/replayed messages.
  const size_t check = min(strikeCount_, static_cast<size_t>(12));
  for (size_t i = 0; i < check; ++i) {
    const size_t idx = (strikeWrite_ + kMaxStrikes - 1 - i) % kMaxStrikes;
    const Strike& old = strikes_[idx];
    if (old.epochSec == epochSec && fabsf(old.lat - lat) < 0.00001f &&
        fabsf(old.lon - lon) < 0.00001f) {
      return false;
    }
  }

  strikes_[strikeWrite_] = {lat, lon, epochSec};
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
                                  uint16_t color) const {
  if (!destination || width == 0 || height == 0) return;

  // Continuous zig-zag bolt. The previous icon was a sparse bitmap and its
  // isolated pixels looked like small squares on the 7-inch panel. Here each
  // segment is a connected line with a subtle antialiased halo and dark
  // one-pixel shadow, while the centre line keeps the exact trail age colour.
  auto blendPixel = [&](int px, int py, uint16_t source, uint8_t alpha) {
    if (px < 0 || py < 0 || px >= static_cast<int>(width) ||
        py >= static_cast<int>(height) || alpha == 0) {
      return;
    }

    const size_t index = static_cast<size_t>(py) * width + px;
    if (alpha == 255) {
      destination[index] = source;
      return;
    }

    const uint16_t background = destination[index];
    const uint32_t sr = (source >> 11) & 0x1FU;
    const uint32_t sg = (source >> 5) & 0x3FU;
    const uint32_t sb = source & 0x1FU;
    const uint32_t br = (background >> 11) & 0x1FU;
    const uint32_t bg = (background >> 5) & 0x3FU;
    const uint32_t bb = background & 0x1FU;
    const uint32_t inv = 255U - alpha;

    const uint16_t rr = static_cast<uint16_t>((sr * alpha + br * inv + 127U) / 255U);
    const uint16_t rg = static_cast<uint16_t>((sg * alpha + bg * inv + 127U) / 255U);
    const uint16_t rb = static_cast<uint16_t>((sb * alpha + bb * inv + 127U) / 255U);
    destination[index] = static_cast<uint16_t>((rr << 11) | (rg << 5) | rb);
  };

  auto drawSegment = [&](int x0, int y0, int x1, int y1, uint16_t c,
                         uint8_t coreAlpha, uint8_t haloAlpha) {
    const int dx = abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
      // Put the soft pixels perpendicular to the dominant line direction.
      if (dx >= -dy) {
        blendPixel(x0, y0 - 1, c, haloAlpha);
        blendPixel(x0, y0 + 1, c, haloAlpha);
      } else {
        blendPixel(x0 - 1, y0, c, haloAlpha);
        blendPixel(x0 + 1, y0, c, haloAlpha);
      }
      blendPixel(x0, y0, c, coreAlpha);

      if (x0 == x1 && y0 == y1) break;
      const int e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
  };

  auto drawBolt = [&](int ox, int oy, uint16_t c, uint8_t coreAlpha,
                      uint8_t haloAlpha) {
    // A compact 13 px tall lightning shape with a visible centre knee.
    drawSegment(ox + 2, oy - 6, ox - 2, oy - 1, c, coreAlpha, haloAlpha);
    drawSegment(ox - 2, oy - 1, ox + 1, oy - 1, c, coreAlpha, haloAlpha);
    drawSegment(ox + 1, oy - 1, ox - 2, oy + 6, c, coreAlpha, haloAlpha);
  };

  const uint16_t shadow = rgb565(7, 11, 15);
  drawBolt(x + 1, y + 1, shadow, 210, 55);
  drawBolt(x, y, color, 255, 85);
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
      drawStrike(destination, width, height, x, y, color);
      ++rendered;
    }
  }
  return rendered > 0;
}
