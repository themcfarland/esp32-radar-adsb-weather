#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <time.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "config.h"
#include "map_viewport.h"
#include "psram_allocator.h"


class LightningService {
 public:
  LightningService();
  ~LightningService();

  bool begin();

  // Fast main-loop facade. The blocking WebSocket/TLS receiver itself runs in
  // a dedicated FreeRTOS task so a large LightningMaps frame can never stall
  // the WebServer, LVGL loop or delivery of background ADS-B results.
  bool loop(bool enabled);

  // Temporarily release the long-lived LightningMaps WSS/TLS session so a
  // serialized HTTPS job can obtain a large contiguous internal-RAM block.
  // The wait runs only in NetworkWorker, never in Arduino/UI loop().
  bool pauseForExternalTls(uint32_t timeoutMs);
  void resumeAfterExternalTls();
  bool externalTlsPaused() const { return externalTlsPaused_.load(); }
  uint32_t externalTlsPauseCount() const { return externalTlsPauseCount_.load(); }

  // Draw the current realtime lightning trail independently of the CHMI
  // radar animation. Colours are based only on strike age versus current time.
  bool renderLive(uint16_t* destination, uint16_t width, uint16_t height,
                  const MapViewport& viewport) const;

  bool ready() const;
  size_t strikeCount() const;
  size_t lastFrameBytes() const { return lastFrameBytes_; }
  size_t largestFrameBytes() const { return largestFrameBytes_; }
  uint32_t jsonMessages() const { return jsonMessages_; }
  uint32_t jsonErrors() const { return jsonErrors_; }
  uint32_t jsonControlMessages() const { return jsonControlMessages_; }
  uint32_t largeFramesSkipped() const { return largeFramesSkipped_; }
  bool workerRunning() const { return workerRunning_.load(); }
  uint32_t workerStackMinBytes() const { return workerStackMinBytes_.load(); }
  uint32_t strokesReceived() const { return strokesReceived_; }
  uint32_t strokesAccepted() const { return strokesAccepted_; }
  uint32_t strokesOutsideMap() const { return strokesOutsideMap_; }
  uint32_t strokesDuplicates() const { return strokesDuplicates_; }
  uint32_t strokesInvalid() const { return strokesInvalid_; }
  uint32_t disconnectCount() const { return disconnectCount_; }
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
  static constexpr size_t kJsonCapacity = 128U * 1024U;
  // The initial live2 history burst can exceed 50 KiB. It is not needed for
  // realtime display and is deliberately not deserialized; later live batches
  // are normally only a few KiB. The WebSocket layer still drains the frame.
  static constexpr size_t kMaxJsonParseBytes = 32U * 1024U;
  static constexpr uint32_t kWorkerStackBytes = 8192U;
  static constexpr uint32_t kHistorySeconds =
      Config::LIGHTNING_TRAIL_RED_MAX_AGE_SEC + 120;

  static void workerTaskEntry(void* context);
  bool ensureWorkerTask();
  void workerTaskLoop();
  void serviceSocketOnce();
  void connectServer();
  void forceReconnect(const char* reason);
  void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length);
  bool handleJsonMessage(const uint8_t* payload, size_t length);
  bool appendFragment(const uint8_t* payload, size_t length);
  void resetFragmentBuffer();
  void recordFrameDiagnostics(size_t length);
  String buildSubscription() const;
  bool addStrike(uint32_t epochSec, float lat, float lon, uint32_t id);
  void pruneOldStrikes(uint32_t nowEpoch);
  void drawStrike(uint16_t* destination, uint16_t width, uint16_t height,
                  int x, int y, uint16_t color, uint32_t ageSec) const;
  uint16_t trailColorForAge(uint32_t ageSec) const;
  int mapX(float lon, uint16_t width, const MapViewport& viewport) const;
  int mapY(float lat, uint16_t height, const MapViewport& viewport) const;

  WebSocketsClient webSocket_;
  TaskHandle_t workerTask_ = nullptr;
  mutable SemaphoreHandle_t strikeMutex_ = nullptr;
  Strike* strikes_ = nullptr;
  BasicJsonDocument<PsramAllocator>* jsonDoc_ = nullptr;
  uint8_t* fragmentBuffer_ = nullptr;
  size_t fragmentLength_ = 0;
  size_t fragmentCapacity_ = 0;
  size_t strikeCount_ = 0;
  size_t strikeWrite_ = 0;

  bool socketStarted_ = false;
  bool connected_ = false;
  std::atomic<bool> enabledRequested_{false};
  std::atomic<bool> dataChanged_{false};
  std::atomic<bool> workerRunning_{false};
  std::atomic<bool> workerStop_{false};
  std::atomic<bool> externalTlsPauseRequested_{false};
  std::atomic<bool> externalTlsPaused_{false};
  std::atomic<uint32_t> externalTlsPauseCount_{0};
  std::atomic<uint32_t> workerStackMinBytes_{0};
  uint32_t nextWorkerCreateAttemptMs_ = 0;
  uint32_t lastPruneMs_ = 0;
  uint32_t lastWorkerStackCheckMs_ = 0;
  uint32_t reconnectAtMs_ = 0;
  uint32_t lastSuccessMs_ = 0;
  uint32_t connectedAtMs_ = 0;
  uint32_t lastValidFrameMs_ = 0;
  uint32_t lastAgeRedrawMs_ = 0;
  uint32_t jsonMessages_ = 0;
  uint32_t jsonErrors_ = 0;
  uint32_t jsonControlMessages_ = 0;
  uint32_t largeFramesSkipped_ = 0;
  uint32_t strokesReceived_ = 0;
  uint32_t strokesAccepted_ = 0;
  uint32_t strokesOutsideMap_ = 0;
  uint32_t strokesDuplicates_ = 0;
  uint32_t strokesInvalid_ = 0;
  uint32_t disconnectCount_ = 0;
  size_t lastFrameBytes_ = 0;
  size_t largestFrameBytes_ = 0;
  size_t lastFrameStrokeCount_ = 0;
  size_t lastFrameAccepted_ = 0;
  uint32_t watchdogReconnects_ = 0;
  bool forcedDisconnect_ = false;
  char status_[128] = "Blesky: LightningMaps ceka";
};
