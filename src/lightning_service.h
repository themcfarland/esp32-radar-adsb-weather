#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <time.h>

#include "config.h"
#include "map_viewport.h"

class RadarService;

class LightningService {
 public:
  LightningService();
  ~LightningService();

  bool begin();

  // Keep the six lightning time slots identical to the CHMI radar timeline.
  // Blitzortung itself is realtime-only; history fills progressively after boot.
  bool updateForRadar(const RadarService& radar);

  // Must be called frequently from Arduino loop(). Returns true when a newly
  // received strike affects the visible map and a redraw is useful.
  bool loop(bool enabled);

  bool renderFrame(uint8_t frameIndex, uint16_t* destination,
                   uint16_t width, uint16_t height,
                   const MapViewport& viewport) const;

  bool ready() const;
  bool frameReady(uint8_t index) const;
  uint8_t frameCount() const { return frameCount_; }
  size_t strikeCount() const { return strikeCount_; }
  bool connected() const { return connected_; }
  const char* status() const { return status_; }
  uint32_t lastSuccessMs() const { return lastSuccessMs_; }

  // True when at least one buffered realtime strike is recent enough and
  // geographically inside radiusKm from the supplied home/station position.
  bool recentStrikeWithin(float centerLat, float centerLon, float radiusKm,
                          uint32_t maxAgeSec) const;

 private:
  struct Strike {
    float lat = 0.0f;
    float lon = 0.0f;
    uint32_t epochSec = 0;
  };

  static constexpr size_t kMaxStrikes = 4096;
  static constexpr uint16_t kLzwDictionarySize = 16384;
  static constexpr uint32_t kHistorySeconds =
      Config::RADAR_FRAME_COUNT * Config::RADAR_STEP_SECONDS + 120;

  void connectCurrentServer();
  void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length);
  bool handleCompressedMessage(const uint8_t* payload, size_t length);
  bool decodeHeaderLzw(const uint8_t* payload, size_t length, String& decoded);
  bool decodeUtf8Code(const uint8_t* payload, size_t length, size_t& offset,
                      uint16_t& code) const;
  bool appendDictionaryEntry(uint16_t code, uint16_t nextCode,
                             String& output, uint8_t& firstChar);
  bool extractStrikeHeader(const String& decoded, uint64_t& timeNs,
                           float& lat, float& lon) const;
  bool parseUnsignedField(const String& text, const char* key,
                          uint64_t& value) const;
  bool parseFloatField(const String& text, const char* key, float& value) const;
  bool addStrike(uint32_t epochSec, float lat, float lon);
  void pruneOldStrikes(uint32_t nowEpoch);
  void drawStrike(uint16_t* destination, uint16_t width, uint16_t height,
                  int x, int y, uint16_t color) const;
  int mapX(float lon, uint16_t width, const MapViewport& viewport) const;
  int mapY(float lat, uint16_t height, const MapViewport& viewport) const;

  WebSocketsClient webSocket_;
  Strike* strikes_ = nullptr;
  size_t strikeCount_ = 0;
  size_t strikeWrite_ = 0;

  uint16_t* lzwPrefix_ = nullptr;
  uint8_t* lzwSuffix_ = nullptr;
  uint8_t* lzwFirst_ = nullptr;
  uint8_t* lzwStack_ = nullptr;

  time_t radarFrameTimes_[Config::RADAR_FRAME_COUNT] = {};
  uint8_t frameCount_ = 0;

  bool socketStarted_ = false;
  bool connected_ = false;
  bool dataChanged_ = false;
  uint8_t serverIndex_ = 0;
  uint32_t reconnectAtMs_ = 0;
  uint32_t lastSuccessMs_ = 0;
  uint32_t decodedMessages_ = 0;
  uint32_t rejectedMessages_ = 0;
  char status_[128] = "Blesky: Blitzortung ceka";
};
