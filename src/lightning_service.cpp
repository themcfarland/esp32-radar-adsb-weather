#include "lightning_service.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


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

bool insideLightningMap(float lat, float lon) {
  constexpr float margin = 0.35f;
  return lat >= Config::MAP_LAT_BOTTOM - margin &&
         lat <= Config::MAP_LAT_TOP + margin &&
         lon >= Config::MAP_LON_LEFT - margin &&
         lon <= Config::MAP_LON_RIGHT + margin;
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
  workerStop_.store(true);
  enabledRequested_.store(false);
  externalTlsPauseRequested_.store(false);
  externalTlsPaused_.store(false);
  if (workerTask_) xTaskNotifyGive(workerTask_);
  webSocket_.disconnect();
  if (strikes_) heap_caps_free(strikes_);
  resetFragmentBuffer();
  delete jsonDoc_;
  if (strikeMutex_) vSemaphoreDelete(strikeMutex_);
}

bool LightningService::begin() {
  strikes_ = static_cast<Strike*>(heap_caps_calloc(
      kMaxStrikes, sizeof(Strike), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  jsonDoc_ = new BasicJsonDocument<PsramAllocator>(kJsonCapacity);
  strikeMutex_ = xSemaphoreCreateMutex();

  if (!strikes_ || !jsonDoc_ || !strikeMutex_) {
    snprintf(status_, sizeof(status_), "Blesky: malo RAM/PSRAM pro LightningMaps");
    return false;
  }

  webSocket_.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
    onWebSocketEvent(type, payload, length);
  });
  webSocket_.setReconnectInterval(60000);  // service handles reconnect itself
  webSocket_.enableHeartbeat(15000, 3000, 2);
  snprintf(status_, sizeof(status_), "Blesky: LightningMaps worker pripraven");
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
  resetFragmentBuffer();
  reconnectAtMs_ = millis() + Config::LIGHTNING_WATCHDOG_RECONNECT_DELAY_MS;
  ++watchdogReconnects_;
  snprintf(status_, sizeof(status_), "Blesky: JSON watchdog reconnect (%u)",
           static_cast<unsigned>(watchdogReconnects_));
}

bool LightningService::pauseForExternalTls(uint32_t timeoutMs) {
  // If the layer/worker is not active there is no persistent TLS session to
  // release. Do not create the worker just for this coordination request.
  if (!workerTask_ || !enabledRequested_.load()) return true;

  externalTlsPauseRequested_.store(true);
  if (workerTask_) xTaskNotifyGive(workerTask_);

  const uint32_t started = millis();
  while (static_cast<uint32_t>(millis() - started) < timeoutMs) {
    if (externalTlsPaused_.load()) return true;
    vTaskDelay(pdMS_TO_TICKS(5));
  }

  Serial.printf("Lightning: external TLS yield timeout after %u ms\n",
                static_cast<unsigned>(timeoutMs));
  return false;
}

void LightningService::resumeAfterExternalTls() {
  externalTlsPauseRequested_.store(false);
  if (workerTask_) xTaskNotifyGive(workerTask_);
}

void LightningService::workerTaskEntry(void* context) {
  auto* self = static_cast<LightningService*>(context);
  if (!self) {
    vTaskDelete(nullptr);
    return;
  }
  self->workerTaskLoop();
}

bool LightningService::ensureWorkerTask() {
  if (workerTask_) return true;
  const uint32_t nowMs = millis();
  if (nextWorkerCreateAttemptMs_ != 0U &&
      static_cast<int32_t>(nowMs - nextWorkerCreateAttemptMs_) < 0) {
    return false;
  }

  // Keep the synchronous WebSocket receiver away from Arduino loop(). Core 1
  // is shared fairly with loopTask at the same low priority; blocking socket
  // waits yield to FreeRTOS and therefore cannot prevent WebServer handling.
  const BaseType_t created = xTaskCreatePinnedToCore(
      workerTaskEntry, "lightning-wss", kWorkerStackBytes, this, 1,
      &workerTask_, 1);
  if (created != pdPASS) {
    workerTask_ = nullptr;
    nextWorkerCreateAttemptMs_ = nowMs + 30000U;
    snprintf(status_, sizeof(status_), "Blesky: worker nelze spustit");
    Serial.printf("Lightning: worker task creation failed, heap=%u largest=%u\n",
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(
                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    return false;
  }

  nextWorkerCreateAttemptMs_ = 0;
  Serial.printf("Lightning: isolated worker started core=1 stack=%u B\n",
                static_cast<unsigned>(kWorkerStackBytes));
  return true;
}

void LightningService::workerTaskLoop() {
  workerRunning_.store(true);
  Serial.printf("Lightning: worker running on core %d\n", xPortGetCoreID());

  while (!workerStop_.load()) {
    serviceSocketOnce();
    const uint32_t stackNowMs = millis();
    if (lastWorkerStackCheckMs_ == 0U ||
        stackNowMs - lastWorkerStackCheckMs_ >= 5000U) {
      lastWorkerStackCheckMs_ = stackNowMs;
      workerStackMinBytes_.store(
          static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr)));
    }
    // A short notified sleep prevents busy-spinning when no frame is pending.
    // xTaskNotifyGive() from loop() wakes this immediately after enable changes.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(4));
  }

  if (socketStarted_) webSocket_.disconnect();
  connected_ = false;
  socketStarted_ = false;
  workerRunning_.store(false);
  workerTask_ = nullptr;
  vTaskDelete(nullptr);
}

void LightningService::serviceSocketOnce() {
  const bool enabled = enabledRequested_.load();

  if (externalTlsPauseRequested_.load()) {
    // A worker HTTPS transaction needs the contiguous SRAM currently held by
    // the long-lived WSS/TLS context. Disconnect once and acknowledge only
    // after the socket has been released. The main/UI task never waits here.
    if (!externalTlsPaused_.load()) {
      if (socketStarted_ || connected_) {
        forcedDisconnect_ = true;
        webSocket_.disconnect();
        connected_ = false;
        socketStarted_ = false;
        connectedAtMs_ = 0;
        lastValidFrameMs_ = 0;
        resetFragmentBuffer();
        reconnectAtMs_ = millis();
      }
      externalTlsPauseCount_.fetch_add(1U);
      externalTlsPaused_.store(true);
      snprintf(status_, sizeof(status_), "Blesky: TLS pauza pro HTTPS");
      Serial.printf("Lightning: WSS yielded for external TLS (count=%u)\n",
                    static_cast<unsigned>(externalTlsPauseCount_.load()));
    }
    return;
  }

  if (externalTlsPaused_.exchange(false)) {
    // Reconnect immediately after the external transaction; this is an
    // intentional yield, not a network failure, so no 5 s failure delay.
    reconnectAtMs_ = millis();
    snprintf(status_, sizeof(status_), "Blesky: obnovuji WSS po HTTPS");
  }

  if (!enabled || WiFi.status() != WL_CONNECTED) {
    if (socketStarted_) {
      webSocket_.disconnect();
      socketStarted_ = false;
      connected_ = false;
      resetFragmentBuffer();
    }
    snprintf(status_, sizeof(status_), enabled ? "Blesky: WiFi offline"
                                               : "Blesky: vrstva vypnuta");
    return;
  }

  if (!socketStarted_ && static_cast<int32_t>(millis() - reconnectAtMs_) >= 0) {
    connectServer();
  }

  // This may wait for an entire WebSocket payload. It is intentionally here,
  // never in Arduino loop(), so even a slow >50 KiB history frame cannot stall
  // the local web UI or snapshot consumption from NetworkWorker.
  if (socketStarted_) webSocket_.loop();

  // A valid envelope counts even when strokes[] is empty; heartbeat/control
  // frames keep the stream watchdog alive during locally quiet weather.
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

  const uint32_t nowMs = millis();
  if (nowMs - lastPruneMs_ >= 5000U) {
    lastPruneMs_ = nowMs;
    const time_t now = time(nullptr);
    if (now > 1700000000) pruneOldStrikes(static_cast<uint32_t>(now));
  }
}

bool LightningService::loop(bool enabled) {
  const bool requested = enabled && ready();
  const bool previous = enabledRequested_.exchange(requested);

  bool workerCreated = false;
  if (requested && !workerTask_) workerCreated = ensureWorkerTask();
  if (workerTask_ && (workerCreated || previous != requested)) {
    xTaskNotifyGive(workerTask_);
  }

  bool changed = dataChanged_.exchange(false);
  const uint32_t nowMs = millis();
  if (strikeCount() > 0 &&
      nowMs - lastAgeRedrawMs_ >= Config::LIGHTNING_REDRAW_MS) {
    lastAgeRedrawMs_ = nowMs;
    changed = true;
  }
  return changed;
}

size_t LightningService::strikeCount() const {
  if (!strikeMutex_) return strikeCount_;
  if (xSemaphoreTake(strikeMutex_, pdMS_TO_TICKS(10)) != pdTRUE)
    return strikeCount_;
  const size_t count = strikeCount_;
  xSemaphoreGive(strikeMutex_);
  return count;
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
      ++disconnectCount_;
      connected_ = false;
      socketStarted_ = false;
      connectedAtMs_ = 0;
      lastValidFrameMs_ = 0;
      resetFragmentBuffer();
      reconnectAtMs_ = millis() + kReconnectDelayMs;
      snprintf(status_, sizeof(status_), "Blesky: WSS odpojen, reconnect");
      break;

    case WStype_TEXT:
      if (payload && length > 0) {
        recordFrameDiagnostics(length);
        handleJsonMessage(payload, length);
      }
      break;

    case WStype_FRAGMENT_TEXT_START:
      resetFragmentBuffer();
      if (!appendFragment(payload, length)) {
        forceReconnect("fragment buffer allocation failed");
      }
      break;

    case WStype_FRAGMENT:
      if (!appendFragment(payload, length)) {
        forceReconnect("fragment buffer allocation failed");
      }
      break;

    case WStype_FRAGMENT_FIN:
      if (!appendFragment(payload, length)) {
        forceReconnect("fragment buffer allocation failed");
        break;
      }
      if (fragmentBuffer_ && fragmentLength_ > 0) {
        recordFrameDiagnostics(fragmentLength_);
        handleJsonMessage(fragmentBuffer_, fragmentLength_);
      }
      resetFragmentBuffer();
      break;

    default:
      break;
  }
}

void LightningService::recordFrameDiagnostics(size_t length) {
  lastFrameBytes_ = length;
  if (length > largestFrameBytes_) largestFrameBytes_ = length;

  const size_t internalFree =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t internalLargest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t psramFree =
      heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (jsonMessages_ < 3U || length >= 32U * 1024U ||
      (jsonMessages_ & 0x7FU) == 0U) {
    Serial.printf(
        "Lightning RX: %u B (max %u), heap=%u largest=%u, psram=%u\n",
        static_cast<unsigned>(length),
        static_cast<unsigned>(largestFrameBytes_),
        static_cast<unsigned>(internalFree),
        static_cast<unsigned>(internalLargest),
        static_cast<unsigned>(psramFree));
  }
}

void LightningService::resetFragmentBuffer() {
  if (fragmentBuffer_) {
    heap_caps_free(fragmentBuffer_);
    fragmentBuffer_ = nullptr;
  }
  fragmentLength_ = 0;
  fragmentCapacity_ = 0;
}

bool LightningService::appendFragment(const uint8_t* payload, size_t length) {
  if (!payload || length == 0U) return true;
  if (fragmentLength_ > SIZE_MAX - length - 1U) return false;

  const size_t needed = fragmentLength_ + length + 1U;
  if (needed > fragmentCapacity_) {
    size_t next = fragmentCapacity_ ? fragmentCapacity_ : 16U * 1024U;
    while (next < needed) {
      if (next > 256U * 1024U) {
        next = needed;
        break;
      }
      next *= 2U;
    }
    void* resized = heap_caps_realloc(
        fragmentBuffer_, next, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!resized) return false;
    fragmentBuffer_ = static_cast<uint8_t*>(resized);
    fragmentCapacity_ = next;
  }

  memcpy(fragmentBuffer_ + fragmentLength_, payload, length);
  fragmentLength_ += length;
  fragmentBuffer_[fragmentLength_] = 0;
  return true;
}

bool LightningService::handleJsonMessage(const uint8_t* payload,
                                         size_t length) {
  if (!payload || length == 0U) return false;

  // live2 can send a large historical burst immediately after subscribe. The
  // dashboard only needs realtime data, so drain that WebSocket frame in the
  // worker but do not spend CPU/PSRAM constructing a huge ArduinoJson DOM.
  if (length > kMaxJsonParseBytes) {
    ++largeFramesSkipped_;
    lastSuccessMs_ = millis();
    lastValidFrameMs_ = lastSuccessMs_;
    if (largeFramesSkipped_ <= 3U || (largeFramesSkipped_ & 0x0FU) == 1U) {
      Serial.printf(
          "Lightning: skipped large history frame %u B (limit %u B, skipped=%u)\n",
          static_cast<unsigned>(length),
          static_cast<unsigned>(kMaxJsonParseBytes),
          static_cast<unsigned>(largeFramesSkipped_));
    }
    snprintf(status_, sizeof(status_),
             "Blesky: LIVE, velky frame %u kB preskocen",
             static_cast<unsigned>((length + 1023U) / 1024U));
    return true;
  }

  // The first server message is a small control/hello envelope containing
  // cid/con/port/time/k but no strokes array. It proves the WSS session is
  // alive and must not be counted as a malformed lightning data frame.
  if (length < 512U) {
    StaticJsonDocument<384> control;
    const DeserializationError controlError =
        deserializeJson(control, payload, length);
    if (!controlError && control["cid"].is<uint32_t>() &&
        control["time"].is<double>()) {
      lastValidFrameMs_ = millis();
      Serial.printf("Lightning: server hello cid=%u con=%u port=%s\n",
                    control["cid"].as<unsigned>(),
                    control["con"] | 0U,
                    control["port"] | "-");
      return true;
    }
  }

  // Parse only the fields needed by the ESP32. The filtered DOM itself lives
  // in PSRAM and is deliberately sized for large initial LightningMaps batches
  // (the browser feed has been observed above 50 KiB / about 500 strokes).
  // small even when the server batches many strokes in one WebSocket frame.
  StaticJsonDocument<512> filter;
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
      const size_t sampleLen = min(length, static_cast<size_t>(96));
      Serial.print("Lightning: payload sample: ");
      for (size_t i = 0; i < sampleLen; ++i) {
        const char c = static_cast<char>(payload[i]);
        Serial.print((c >= 32 && c <= 126) ? c : '.');
      }
      Serial.println();
    }
    return false;
  }

  // LightningMaps also emits valid time/control envelopes without strokes[].
  // They are feed keep-alives, not parser errors. Counting them as errors made
  // a healthy socket look broken in diagnostics (for example tiny ~20-40 B
  // frames between actual stroke batches).
  const bool hasTime = (*jsonDoc_)["time"].is<uint32_t>() ||
                       (*jsonDoc_)["time"].is<uint64_t>() ||
                       (*jsonDoc_)["time"].is<double>();
  if (!hasTime) {
    ++jsonErrors_;
    if (jsonErrors_ <= 5U || (jsonErrors_ & 0x3FU) == 1U) {
      Serial.printf("Lightning: valid JSON without time, bytes=%u, errors=%u\n",
                    static_cast<unsigned>(length),
                    static_cast<unsigned>(jsonErrors_));
    }
    return false;
  }

  ++jsonMessages_;
  lastSuccessMs_ = millis();
  lastValidFrameMs_ = lastSuccessMs_;

  if (!(*jsonDoc_)["strokes"].is<JsonArray>()) {
    ++jsonControlMessages_;
    if (jsonControlMessages_ <= 3U || (jsonControlMessages_ & 0x7FU) == 1U) {
      Serial.printf("Lightning JSON control: frame=%u B, control=%u, errors=%u\n",
                    static_cast<unsigned>(length),
                    static_cast<unsigned>(jsonControlMessages_),
                    static_cast<unsigned>(jsonErrors_));
    }
    snprintf(status_, sizeof(status_),
             "Blesky: LIVE %u bodu, heartbeat",
             static_cast<unsigned>(strikeCount_));
    return true;
  }

  JsonArray strokes = (*jsonDoc_)["strokes"].as<JsonArray>();
  lastFrameStrokeCount_ = strokes.size();
  size_t accepted = 0;
  size_t outside = 0;
  size_t duplicates = 0;
  size_t invalid = 0;
  for (JsonObject stroke : strokes) {
    const uint64_t timeMs = stroke["time"] | 0ULL;
    const float lat = stroke["lat"] | NAN;
    const float lon = stroke["lon"] | NAN;
    const uint32_t id = stroke["id"] | 0U;

    if (timeMs < 1700000000000ULL || !isfinite(lat) || !isfinite(lon) ||
        fabsf(lat) > 90.0f || fabsf(lon) > 180.0f) {
      ++invalid;
      ++strokesInvalid_;
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

    if (!insideLightningMap(lat, lon)) {
      ++outside;
      ++strokesOutsideMap_;
      continue;
    }

    if (addStrike(epochSec, lat, lon, id)) {
      ++accepted;
      ++strokesAccepted_;
      dataChanged_.store(true);
    } else {
      ++duplicates;
      ++strokesDuplicates_;
    }
  }

  lastFrameAccepted_ = accepted;
  if (jsonMessages_ <= 5U || accepted > 0U || (jsonMessages_ & 0x7FU) == 1U) {
    Serial.printf(
        "Lightning JSON: frame=%u B strokes=%u accepted=%u outside=%u dup=%u invalid=%u total=%u errors=%u\n",
        static_cast<unsigned>(length),
        static_cast<unsigned>(lastFrameStrokeCount_),
        static_cast<unsigned>(accepted),
        static_cast<unsigned>(outside),
        static_cast<unsigned>(duplicates),
        static_cast<unsigned>(invalid),
        static_cast<unsigned>(strikeCount_),
        static_cast<unsigned>(jsonErrors_));
  }
  snprintf(status_, sizeof(status_),
           "Blesky: LIVE %u bodu, RX %u kB (%u/%u, mimo %u)",
           static_cast<unsigned>(strikeCount_),
           static_cast<unsigned>((length + 1023U) / 1024U),
           static_cast<unsigned>(accepted),
           static_cast<unsigned>(lastFrameStrokeCount_),
           static_cast<unsigned>(outside));
  return true;
}

bool LightningService::addStrike(uint32_t epochSec, float lat, float lon,
                                 uint32_t id) {
  if (!strikeMutex_ ||
      xSemaphoreTake(strikeMutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }

  // Do not trust the subscription viewport as a server-side filter. Current
  // live2 batches can contain many European strikes outside p[]. Keep only the
  // map area (+ margin) in the persistent ESP32 strike buffer.
  constexpr float margin = 0.35f;
  if (lat < Config::MAP_LAT_BOTTOM - margin || lat > Config::MAP_LAT_TOP + margin ||
      lon < Config::MAP_LON_LEFT - margin || lon > Config::MAP_LON_RIGHT + margin) {
    xSemaphoreGive(strikeMutex_);
    return false;
  }

  // LightningMaps provides a stable stroke id. Prefer it for duplicate
  // suppression after reconnects/replayed batches; retain a coordinate/time
  // fallback in case an id is ever missing.
  const size_t check = min(strikeCount_, static_cast<size_t>(32));
  for (size_t i = 0; i < check; ++i) {
    const size_t idx = (strikeWrite_ + kMaxStrikes - 1 - i) % kMaxStrikes;
    const Strike& old = strikes_[idx];
    if (id != 0U && old.id == id) {
      xSemaphoreGive(strikeMutex_);
      return false;
    }
    if (old.epochSec == epochSec && fabsf(old.lat - lat) < 0.00001f &&
        fabsf(old.lon - lon) < 0.00001f) {
      xSemaphoreGive(strikeMutex_);
      return false;
    }
  }

  strikes_[strikeWrite_] = {lat, lon, epochSec, id};
  strikeWrite_ = (strikeWrite_ + 1) % kMaxStrikes;
  if (strikeCount_ < kMaxStrikes) ++strikeCount_;
  xSemaphoreGive(strikeMutex_);
  return true;
}

void LightningService::pruneOldStrikes(uint32_t nowEpoch) {
  if (!strikes_ || strikeCount_ == 0 || !strikeMutex_) return;
  if (xSemaphoreTake(strikeMutex_, pdMS_TO_TICKS(20)) != pdTRUE) return;
  const uint32_t cutoff = nowEpoch > kHistorySeconds ? nowEpoch - kHistorySeconds : 0;
  for (size_t i = 0; i < kMaxStrikes; ++i) {
    if (strikes_[i].epochSec != 0 && strikes_[i].epochSec < cutoff) {
      strikes_[i].epochSec = 0;
      if (strikeCount_ > 0) --strikeCount_;
    }
  }
  xSemaphoreGive(strikeMutex_);
}

bool LightningService::recentStrikeWithin(float centerLat, float centerLon,
                                          float radiusKm,
                                          uint32_t maxAgeSec) const {
  if (!strikes_ || strikeCount_ == 0 || radiusKm <= 0.0f ||
      !isfinite(centerLat) || !isfinite(centerLon) || !strikeMutex_) {
    return false;
  }

  const time_t nowTime = time(nullptr);
  if (nowTime <= 1700000000) return false;
  if (xSemaphoreTake(strikeMutex_, pdMS_TO_TICKS(10)) != pdTRUE) return false;
  const uint32_t nowEpoch = static_cast<uint32_t>(nowTime);

  for (size_t i = 0; i < kMaxStrikes; ++i) {
    const Strike& strike = strikes_[i];
    if (strike.epochSec == 0 || strike.epochSec > nowEpoch + 5U) continue;
    if (nowEpoch - strike.epochSec > maxAgeSec) continue;
    if (greatCircleDistanceKm(centerLat, centerLon, strike.lat, strike.lon) <=
        radiusKm) {
      xSemaphoreGive(strikeMutex_);
      return true;
    }
  }
  xSemaphoreGive(strikeMutex_);
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
  if (!strikeMutex_ ||
      xSemaphoreTake(strikeMutex_, pdMS_TO_TICKS(10)) != pdTRUE) {
    return false;
  }

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
  xSemaphoreGive(strikeMutex_);
  return rendered > 0;
}
