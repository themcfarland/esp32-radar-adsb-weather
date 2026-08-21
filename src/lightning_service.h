#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <time.h>

#include "config.h"
#include "map_viewport.h"
#include "psram_allocator.h"


class LightningService {
 public:
  LightningService();
  ~LightningService();

  bool begin();

  // Must be called frequently from Arduino loop(). Returns true when a newly
  // received strike affects the visible map and a redraw is useful.
  bool loop(bool enabled);

  // Draw the current realtime lightning trail independently of the CHMI
  // radar animation. Colours are based only on strike age versus current time.
  bool renderLive(uint16_t* destination, uint16_t width, uint16_t height,
                  const MapViewport& viewport) const;

  bool ready() const;
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
    uint32_t id = 0;
  };

  static constexpr size_t kMaxStrikes = 4096;
  static constexpr size_t kJsonCapacity = 24576;
  static constexpr uint32_t kHistorySeconds =
      Config::LIGHTNING_TRAIL_RED_MAX_AGE_SEC + 120;

  void connectServer();
  void forceReconnect(const char* reason);
  void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length);
  bool handleJsonMessage(const uint8_t* payload, size_t length);
  String buildSubscription() const;
  bool addStrike(uint32_t epochSec, float lat, float lon, uint32_t id);
  void pruneOldStrikes(uint32_t nowEpoch);
  void drawStrike(uint16_t* destination, uint16_t width, uint16_t height,
                  int x, int y, uint16_t color, uint32_t ageSec) const;
  uint16_t trailColorForAge(uint32_t ageSec) const;
  int mapX(float lon, uint16_t width, const MapViewport& viewport) const;
  int mapY(float lat, uint16_t height, const MapViewport& viewport) const;

  WebSocketsClient webSocket_;
  Strike* strikes_ = nullptr;
  BasicJsonDocument<PsramAllocator>* jsonDoc_ = nullptr;
  size_t strikeCount_ = 0;
  size_t strikeWrite_ = 0;

  bool socketStarted_ = false;
  bool connected_ = false;
  bool dataChanged_ = false;
  uint32_t reconnectAtMs_ = 0;
  uint32_t lastSuccessMs_ = 0;
  uint32_t connectedAtMs_ = 0;
  uint32_t lastValidFrameMs_ = 0;
  uint32_t lastAgeRedrawMs_ = 0;
  uint32_t jsonMessages_ = 0;
  uint32_t jsonErrors_ = 0;
  uint32_t strokesReceived_ = 0;
  uint32_t watchdogReconnects_ = 0;
  bool forcedDisconnect_ = false;
  char status_[128] = "Blesky: LightningMaps ceka";
};
