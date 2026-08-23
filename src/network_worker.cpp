#include "network_worker.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <new>

#include "config.h"
#include "debug_log.h"

namespace {

template <typename T, typename... Args>
T* psramNew(Args&&... args) {
  void* memory = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) return nullptr;
  return new (memory) T(args...);
}

template <typename T>
void psramDelete(T*& object) {
  if (!object) return;
  object->~T();
  heap_caps_free(object);
  object = nullptr;
}

uint8_t countBits(uint8_t value) {
  uint8_t count = 0;
  while (value) {
    count += value & 1U;
    value >>= 1U;
  }
  return count;
}

bool deadlinePending(uint32_t now, uint32_t deadline) {
  return deadline != 0U && static_cast<int32_t>(deadline - now) > 0;
}

}  // namespace

NetworkWorker::NetworkWorker() = default;

NetworkWorker::~NetworkWorker() {
  setPaused(true);
  if (pendingRadar_.overlay) RadarService::discardRuntimeUpdate(pendingRadar_);
  psramDelete(localAdsbWorker_);
  psramDelete(internetAdsbWorker_);
  psramDelete(weatherWorker_);
  psramDelete(pendingLocalAdsb_);
  psramDelete(pendingInternetAdsb_);
  if (stateMutex_) vSemaphoreDelete(stateMutex_);
  if (configMutex_) vSemaphoreDelete(configMutex_);
  if (resultMutex_) vSemaphoreDelete(resultMutex_);
}

bool NetworkWorker::begin(RadarService* radar) {
  if (task_) return true;
  radar_ = radar;
  if (!radar_) return false;

  stateMutex_ = xSemaphoreCreateMutex();
  configMutex_ = xSemaphoreCreateMutex();
  resultMutex_ = xSemaphoreCreateMutex();
  if (!stateMutex_ || !configMutex_ || !resultMutex_) return false;

  localAdsbWorker_ = psramNew<AdsbService>("");
  internetAdsbWorker_ = psramNew<AdsbService>("");
  weatherWorker_ = psramNew<WeatherService>("", "");
  pendingLocalAdsb_ = psramNew<AircraftSnapshot>();
  pendingInternetAdsb_ = psramNew<AircraftSnapshot>();
  if (!localAdsbWorker_ || !internetAdsbWorker_ || !weatherWorker_ ||
      !pendingLocalAdsb_ || !pendingInternetAdsb_) {
    DebugLog::println("Network worker: PSRAM allocation failed");
    return false;
  }

  internetAdsbWorker_->setLocalEnabled(false);

  // ESP-IDF's xTaskCreatePinnedToCore() stack depth is in bytes. Keep the task
  // stack modest; large JSON documents and aircraft caches live in PSRAM.
  const BaseType_t created = xTaskCreatePinnedToCore(
      taskEntry, "network-worker", 12288, this, 1, &task_, 0);
  if (created != pdPASS) {
    task_ = nullptr;
    DebugLog::println("Network worker: task creation failed");
    return false;
  }

  DebugLog::printf("Network worker: started on core 0, stack=12288 B, heap=%u kB\n",
                   static_cast<unsigned>(ESP.getFreeHeap() / 1024));
  return true;
}

void NetworkWorker::configure(const String& localAdsbUrl,
                              bool localAdsbEnabled,
                              const String& wuApiKey,
                              const String& wuStationId,
                              float latitude, float longitude) {
  if (!configMutex_) {
    config_.localAdsbUrl = localAdsbUrl;
    config_.localAdsbEnabled = localAdsbEnabled;
    config_.wuApiKey = wuApiKey;
    config_.wuStationId = wuStationId;
    config_.latitude = latitude;
    config_.longitude = longitude;
    return;
  }
  if (xSemaphoreTake(configMutex_, pdMS_TO_TICKS(50)) != pdTRUE) return;
  config_.localAdsbUrl = localAdsbUrl;
  config_.localAdsbEnabled = localAdsbEnabled;
  config_.wuApiKey = wuApiKey;
  config_.wuStationId = wuStationId;
  config_.latitude = latitude;
  config_.longitude = longitude;
  xSemaphoreGive(configMutex_);
}

bool NetworkWorker::copyConfig(WorkerConfig& target) {
  if (!configMutex_ || xSemaphoreTake(configMutex_, pdMS_TO_TICKS(100)) != pdTRUE)
    return false;
  target = config_;
  xSemaphoreGive(configMutex_);
  return true;
}

bool NetworkWorker::request(Job job, bool force) {
  if (!task_ || !stateMutex_) return false;
  const uint8_t index = static_cast<uint8_t>(job);
  if (index >= kJobCount) return false;

  if (xSemaphoreTake(stateMutex_, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  const uint32_t now = millis();
  if (paused_) {
    xSemaphoreGive(stateMutex_);
    return false;
  }
  if (!force && deadlinePending(now, nextAllowedMs_[index])) {
    ++diagnostics_.backoffSkips;
    xSemaphoreGive(stateMutex_);
    return false;
  }
  if (force) nextAllowedMs_[index] = 0;

  const uint8_t jobBit = jobMask(job);
  if ((pendingMask_ & jobBit) != 0U || runningJob_ == static_cast<int8_t>(index)) {
    xSemaphoreGive(stateMutex_);
    return false;
  }
  pendingMask_ |= jobBit;
  diagnostics_.pendingJobs = countBits(pendingMask_);
  xSemaphoreGive(stateMutex_);
  xTaskNotifyGive(task_);
  return true;
}

void NetworkWorker::requestAll(bool force) {
  // Order is selected by selectNextJob(); requests only set de-duplicated bits.
  request(Job::AdsbLocal, force);
  request(Job::WeatherCurrent, force);
  request(Job::AdsbInternet, force);
  request(Job::Radar, force);
  request(Job::Forecast, force);
}

void NetworkWorker::setPaused(bool paused) {
  if (!stateMutex_) return;
  if (xSemaphoreTake(stateMutex_, pdMS_TO_TICKS(50)) != pdTRUE) return;
  paused_ = paused;
  diagnostics_.paused = paused;
  if (paused) {
    // Do not start more HTTP/TLS transactions. The operation already in flight
    // is allowed to finish because forcibly deleting a task inside mbedTLS can
    // leak sockets/heap and is less safe than completing its bounded timeout.
    pendingMask_ = 0;
    diagnostics_.pendingJobs = 0;
  }
  xSemaphoreGive(stateMutex_);
  if (!paused && task_) xTaskNotifyGive(task_);
}

bool NetworkWorker::busy() const {
  if (!stateMutex_) return false;
  bool value = false;
  if (xSemaphoreTake(stateMutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
    value = runningJob_ >= 0 || pendingMask_ != 0;
    xSemaphoreGive(stateMutex_);
  }
  return value;
}

bool NetworkWorker::consumeTlsRecoveryRequest() {
  if (!stateMutex_ || xSemaphoreTake(stateMutex_, pdMS_TO_TICKS(20)) != pdTRUE)
    return false;
  const bool requested = tlsRecoveryRequested_;
  tlsRecoveryRequested_ = false;
  xSemaphoreGive(stateMutex_);
  return requested;
}

const char* NetworkWorker::jobName(Job job) {
  switch (job) {
    case Job::AdsbLocal: return "ADS-B local";
    case Job::WeatherCurrent: return "weather current";
    case Job::AdsbInternet: return "adsb.fi";
    case Job::Radar: return "CHMI radar";
    case Job::Forecast: return "forecast";
    default: return "unknown";
  }
}

bool NetworkWorker::jobUsesTls(Job job) {
  return job == Job::WeatherCurrent || job == Job::AdsbInternet ||
         job == Job::Radar || job == Job::Forecast;
}

void NetworkWorker::updateTlsMemoryStateLocked(uint32_t freeInternal,
                                                   uint32_t largestInternal) {
  // Critical-state hysteresis: enter at 38/26 kB, leave only after both the
  // free heap and largest contiguous block have recovered comfortably.
  if (tlsGuardLatched_) {
    if (freeInternal >= Config::TLS_GUARD_RECOVER_FREE_INTERNAL &&
        largestInternal >= Config::TLS_GUARD_RECOVER_LARGEST_BLOCK) {
      tlsGuardLatched_ = false;
    }
  } else if (freeInternal < Config::TLS_GUARD_CRITICAL_FREE_INTERNAL ||
             largestInternal < Config::TLS_GUARD_CRITICAL_LARGEST_BLOCK) {
    tlsGuardLatched_ = true;
  }

  uint8_t state = 0;
  if (tlsGuardLatched_) {
    state = 2;
  } else if (largestInternal < Config::TLS_GUARD_WARNING_LARGEST_BLOCK) {
    state = 1;
  }
  diagnostics_.tlsMemoryState = state;
  diagnostics_.tlsGuardLowMemory = state >= 2;
  diagnostics_.tlsFreeInternal = freeInternal;
  diagnostics_.tlsLargestInternalBlock = largestInternal;
}

bool NetworkWorker::tlsPreflight(Job job, char* result, size_t resultSize,
                                 bool& deferred) {
  deferred = false;
  if (!jobUsesTls(job)) return true;

  const uint32_t freeInternal = static_cast<uint32_t>(
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  const uint32_t largestInternal = static_cast<uint32_t>(
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  const uint8_t index = static_cast<uint8_t>(job);

  if (!stateMutex_ || xSemaphoreTake(stateMutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
    snprintf(result, resultSize, "%s: TLS guard lock timeout", jobName(job));
    deferred = true;
    return false;
  }

  updateTlsMemoryStateLocked(freeInternal, largestInternal);

  // Normal and warning states always attempt TLS. This is the key change from
  // 0.30.8: a 30-36 kB largest block is no longer enough to disconnect WSS or
  // postpone an otherwise healthy adsb.fi/CHMI request.
  if (!tlsGuardLatched_) {
    tlsDefers_[index] = 0;
    diagnostics_.tlsConsecutiveDefers = 0;
    xSemaphoreGive(stateMutex_);
    return true;
  }

  // In a genuinely critical state defer only once. Ask LightningMaps to yield
  // its persistent transport as a safety measure, then allow later attempts
  // without repeatedly tearing WSS down. The one-shot flag is cleared only by
  // a successful TLS job.
  if (!tlsFailureRecoveryUsed_[index] && tlsDefers_[index] == 0U) {
    tlsDefers_[index] = 1U;
    tlsFailureRecoveryUsed_[index] = true;
    diagnostics_.tlsConsecutiveDefers = 1U;
    ++diagnostics_.tlsDeferredJobs;
    ++diagnostics_.tlsRecoveryRequests;
    tlsRecoveryRequested_ = true;
    xSemaphoreGive(stateMutex_);

    snprintf(result, resultSize, "%s: TLS critical free=%u largest=%u",
             jobName(job), static_cast<unsigned>(freeInternal),
             static_cast<unsigned>(largestInternal));
    DebugLog::printf(
        "TLS guard: critical defer %s | free=%u largest=%u -> one WSS yield\n",
        jobName(job), static_cast<unsigned>(freeInternal),
        static_cast<unsigned>(largestInternal));
    deferred = true;
    return false;
  }

  tlsDefers_[index] = 0U;
  diagnostics_.tlsConsecutiveDefers = 0U;
  ++diagnostics_.tlsForcedAttempts;
  xSemaphoreGive(stateMutex_);
  DebugLog::printf(
      "TLS guard: critical allowed %s attempt after one-shot recovery | free=%u largest=%u\n",
      jobName(job), static_cast<unsigned>(freeInternal),
      static_cast<unsigned>(largestInternal));
  return true;
}

bool NetworkWorker::requestReactiveTlsRecovery(Job job, bool success) {
  if (success || !jobUsesTls(job)) return false;

  const uint8_t index = static_cast<uint8_t>(job);
  const uint32_t freeInternal = static_cast<uint32_t>(
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  const uint32_t largestInternal = static_cast<uint32_t>(
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

  const bool memoryPressure =
      freeInternal < Config::TLS_RECOVERY_FAILURE_FREE_INTERNAL ||
      largestInternal < Config::TLS_RECOVERY_FAILURE_LARGEST_BLOCK;
  if (!memoryPressure) return false;

  // For adsb.fi distinguish a transport/TLS/body failure from a genuine HTTP
  // provider response. HTTP 4xx/5xx must not tear down LightningMaps.
  if (job == Job::AdsbInternet) {
    const int code = internetAdsbWorker_->lastNetworkHttpCode();
    if (code >= 400 && code < 600) return false;
  }

  if (!stateMutex_ || xSemaphoreTake(stateMutex_, pdMS_TO_TICKS(50)) != pdTRUE)
    return false;

  updateTlsMemoryStateLocked(freeInternal, largestInternal);
  if (tlsFailureRecoveryUsed_[index]) {
    xSemaphoreGive(stateMutex_);
    return false;
  }

  tlsFailureRecoveryUsed_[index] = true;
  tlsRecoveryRequested_ = true;
  ++diagnostics_.tlsRecoveryRequests;
  xSemaphoreGive(stateMutex_);

  DebugLog::printf(
      "TLS recovery: %s failed under memory pressure | free=%u largest=%u -> one WSS yield\n",
      jobName(job), static_cast<unsigned>(freeInternal),
      static_cast<unsigned>(largestInternal));
  return true;
}

bool NetworkWorker::selectNextJob(Job& job) {
  if (!stateMutex_ || xSemaphoreTake(stateMutex_, portMAX_DELAY) != pdTRUE)
    return false;
  if (paused_ || pendingMask_ == 0) {
    diagnostics_.pendingJobs = countBits(pendingMask_);
    xSemaphoreGive(stateMutex_);
    return false;
  }

  // Latency-sensitive LAN ADS-B gets the first slot after any long transfer.
  // The remaining jobs are intentionally serialized so several TLS clients
  // never compete for internal heap / Wi-Fi buffers at the same time.
  static constexpr Job priority[] = {
      // Keep aircraft feeds ahead of all bulk weather/radar work. A slow CHMI
      // index must never be selected while an ADS-B refresh is already queued.
      Job::AdsbLocal, Job::AdsbInternet, Job::WeatherCurrent,
      Job::Radar, Job::Forecast};
  const uint32_t now = millis();
  for (Job candidate : priority) {
    const uint8_t index = static_cast<uint8_t>(candidate);
    const uint8_t candidateBit = jobMask(candidate);
    if ((pendingMask_ & candidateBit) == 0U) continue;
    if (deadlinePending(now, nextAllowedMs_[index])) {
      pendingMask_ &= ~candidateBit;
      ++diagnostics_.backoffSkips;
      continue;
    }
    pendingMask_ &= ~candidateBit;
    runningJob_ = static_cast<int8_t>(index);
    diagnostics_.running = true;
    diagnostics_.pendingJobs = countBits(pendingMask_);
    strlcpy(diagnostics_.activeJob, jobName(candidate),
            sizeof(diagnostics_.activeJob));
    job = candidate;
    xSemaphoreGive(stateMutex_);
    return true;
  }

  diagnostics_.pendingJobs = countBits(pendingMask_);
  xSemaphoreGive(stateMutex_);
  return false;
}

uint32_t NetworkWorker::failureBackoffMs(Job job, uint8_t failures) const {
  const uint8_t exponent = failures > 4U ? 4U : failures;
  uint32_t base = 30000UL;
  uint32_t maximum = 300000UL;
  switch (job) {
    case Job::AdsbLocal:
      base = 5000UL;
      maximum = 30000UL;
      break;
    case Job::AdsbInternet:
      // Internet aircraft are a live overlay. A five-minute backoff was long
      // enough for the 120 s cache to expire, making all adsb.fi aircraft
      // disappear even after a short provider/TLS outage. Cap recovery at 60 s.
      if (failures <= 1U) return Config::ADSB_FI_BACKOFF_FIRST_MS;
      if (failures == 2U) return Config::ADSB_FI_BACKOFF_SECOND_MS;
      return Config::ADSB_FI_BACKOFF_MAX_MS;
    case Job::Radar:
      base = 60000UL;
      maximum = 300000UL;
      break;
    case Job::WeatherCurrent:
      base = 30000UL;
      maximum = 300000UL;
      break;
    case Job::Forecast:
      base = 120000UL;
      maximum = 900000UL;
      break;
    default:
      break;
  }
  uint32_t value = base;
  for (uint8_t i = 1; i < exponent; ++i) {
    if (value >= maximum / 2U) return maximum;
    value *= 2U;
  }
  return value > maximum ? maximum : value;
}

bool NetworkWorker::execute(Job job, char* result, size_t resultSize,
                            bool& changed, bool& deferred) {
  changed = false;
  deferred = false;
  if (result && resultSize) result[0] = '\0';
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(result, resultSize, "%s: Wi-Fi offline", jobName(job));
    return false;
  }

  if (!tlsPreflight(job, result, resultSize, deferred)) return false;

  WorkerConfig cfg;
  if (!copyConfig(cfg)) {
    snprintf(result, resultSize, "%s: config lock timeout", jobName(job));
    return false;
  }

  switch (job) {
    case Job::AdsbLocal: {
      if (!cfg.localAdsbEnabled || cfg.localAdsbUrl.isEmpty()) {
        snprintf(result, resultSize, "ADS-B local: disabled");
        return true;
      }
      localAdsbWorker_->setAircraftUrl(cfg.localAdsbUrl);
      localAdsbWorker_->setLocalEnabled(true);
      const uint32_t before = localAdsbWorker_->lastLocalSuccessMs();
      localAdsbWorker_->updateLocalNow();
      const uint32_t after = localAdsbWorker_->lastLocalSuccessMs();
      const bool success = after != 0U && after != before;
      if (success && resultMutex_ &&
          xSemaphoreTake(resultMutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        *pendingLocalAdsb_ = localAdsbWorker_->snapshot();
        pendingLocalReady_ = pendingLocalAdsb_->valid;
        xSemaphoreGive(resultMutex_);
        changed = pendingLocalReady_;
      }
      snprintf(result, resultSize, "%s", localAdsbWorker_->snapshot().status);
      return success;
    }

    case Job::AdsbInternet: {
      internetAdsbWorker_->setLocalEnabled(false);
      const uint32_t before = internetAdsbWorker_->lastNetworkSuccessMs();
      internetAdsbWorker_->updateNetworkNow();
      const uint32_t after = internetAdsbWorker_->lastNetworkSuccessMs();
      const bool success = after != 0U && after != before;
      if (success && resultMutex_ &&
          xSemaphoreTake(resultMutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        *pendingInternetAdsb_ = internetAdsbWorker_->snapshot();
        pendingInternetReady_ = pendingInternetAdsb_->valid;
        xSemaphoreGive(resultMutex_);
        changed = pendingInternetReady_;
      }
      snprintf(result, resultSize, "%s", internetAdsbWorker_->snapshot().status);
      return success;
    }

    case Job::WeatherCurrent: {
      weatherWorker_->setConfig(cfg.wuApiKey, cfg.wuStationId);
      weatherWorker_->setLocation(cfg.latitude, cfg.longitude);
      const bool success = weatherWorker_->updateCurrent();
      if (success && resultMutex_ &&
          xSemaphoreTake(resultMutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        pendingCurrent_ = weatherWorker_->snapshot();
        pendingCurrentReady_ = true;
        xSemaphoreGive(resultMutex_);
        changed = true;
      }
      snprintf(result, resultSize, "%s", weatherWorker_->snapshot().status);
      return success;
    }

    case Job::Forecast: {
      weatherWorker_->setConfig(cfg.wuApiKey, cfg.wuStationId);
      weatherWorker_->setLocation(cfg.latitude, cfg.longitude);
      const bool success = weatherWorker_->updateForecast();
      if (success && resultMutex_ &&
          xSemaphoreTake(resultMutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        pendingForecast_ = weatherWorker_->snapshot();
        pendingForecastReady_ = true;
        xSemaphoreGive(resultMutex_);
        changed = true;
      }
      snprintf(result, resultSize, "%s", weatherWorker_->snapshot().status);
      return success;
    }

    case Job::Radar: {
      RadarService::RuntimeFrameUpdate fetched;
      const RadarService::RuntimeFetchResult fetch = radar_->fetchRuntimeUpdate(fetched);
      if (fetch == RadarService::RuntimeFetchResult::Failed) {
        snprintf(result, resultSize, "%s", radar_->status());
        RadarService::discardRuntimeUpdate(fetched);
        return false;
      }
      if (fetch == RadarService::RuntimeFetchResult::NoChange) {
        snprintf(result, resultSize, "Radar: bez noveho snimku");
        return true;
      }
      if (resultMutex_ &&
          xSemaphoreTake(resultMutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (pendingRadarReady_) RadarService::discardRuntimeUpdate(pendingRadar_);
        pendingRadar_ = fetched;
        fetched.overlay = nullptr;
        pendingRadarReady_ = true;
        xSemaphoreGive(resultMutex_);
        changed = true;
      } else {
        RadarService::discardRuntimeUpdate(fetched);
        snprintf(result, resultSize, "Radar: result lock timeout");
        return false;
      }
      snprintf(result, resultSize, "Radar: novy snimek pripraven");
      return true;
    }

    default:
      snprintf(result, resultSize, "unknown network job");
      return false;
  }
}

void NetworkWorker::finishJob(Job job, bool success, bool changed, bool deferred,
                              uint32_t durationMs, const char* result) {
  if (!stateMutex_ || xSemaphoreTake(stateMutex_, portMAX_DELAY) != pdTRUE)
    return;
  const uint8_t index = static_cast<uint8_t>(job);
  runningJob_ = -1;
  diagnostics_.running = false;
  strlcpy(diagnostics_.activeJob, "idle", sizeof(diagnostics_.activeJob));
  diagnostics_.lastJobDurationMs = durationMs;
  if (durationMs > diagnostics_.longestJobDurationMs)
    diagnostics_.longestJobDurationMs = durationMs;
  if (!deferred) ++diagnostics_.completedJobs;

  const uint32_t finishedAt = millis();
  const uint32_t startedAt = finishedAt - durationMs;
  if (job == Job::AdsbInternet) {
    if (!deferred) {
      diagnostics_.adsbInternetLastAttemptMs = startedAt;
      diagnostics_.adsbInternetLastDurationMs = durationMs;
      diagnostics_.adsbInternetHttpCode = internetAdsbWorker_->lastNetworkHttpCode();
      strlcpy(diagnostics_.adsbInternetSource, internetAdsbWorker_->networkSource(),
              sizeof(diagnostics_.adsbInternetSource));
      // A pre-flight defer is TLS-guard metadata, not a provider result. Keep
      // the last real adsb.fi/adsb.lol result visible in source diagnostics.
      strlcpy(diagnostics_.adsbInternetResult, result ? result : "",
              sizeof(diagnostics_.adsbInternetResult));
    }
    if (success && !deferred) {
      diagnostics_.adsbInternetLastSuccessMs = finishedAt;
      diagnostics_.adsbInternetLastAircraftCount =
          internetAdsbWorker_->snapshot().adsbFiCount > 0
              ? internetAdsbWorker_->snapshot().adsbFiCount
              : internetAdsbWorker_->snapshot().count;
      diagnostics_.adsbInternetLastMlatCount = internetAdsbWorker_->snapshot().mlatCount;
    }
  } else if (job == Job::AdsbLocal) {
    diagnostics_.adsbLocalLastAttemptMs = startedAt;
    diagnostics_.adsbLocalLastDurationMs = durationMs;
    strlcpy(diagnostics_.adsbLocalResult, result ? result : "",
            sizeof(diagnostics_.adsbLocalResult));
    if (success) diagnostics_.adsbLocalLastSuccessMs = finishedAt;
  }

  if (deferred) {
    nextAllowedMs_[index] = millis() + Config::TLS_GUARD_RETRY_MS;
    snprintf(diagnostics_.lastResult, sizeof(diagnostics_.lastResult),
             "%s: DEFER TLS %us %.48s", jobName(job),
             static_cast<unsigned>(Config::TLS_GUARD_RETRY_MS / 1000UL),
             result ? result : "");
    diagnostics_.pendingJobs = countBits(pendingMask_);
    xSemaphoreGive(stateMutex_);
    DebugLog::printf("NET worker: %s | TLS DEFER | %u ms\n", jobName(job),
                     static_cast<unsigned>(durationMs));
    return;
  }

  // Reactive TLS recovery is intentionally evaluated only after a real failed
  // request. This prevents successful HTTPS traffic at ~35 kB largest-block
  // heap from needlessly disconnecting LightningMaps.
  bool reactiveTlsRecovery = false;
  if (!success && jobUsesTls(job)) {
    // Drop the mutex while probing heap / scheduling the one-shot recovery.
    xSemaphoreGive(stateMutex_);
    reactiveTlsRecovery = requestReactiveTlsRecovery(job, false);
    if (xSemaphoreTake(stateMutex_, portMAX_DELAY) != pdTRUE) return;
  }

  if (success) {
    failures_[index] = 0;
    tlsFailureRecoveryUsed_[index] = false;
    nextAllowedMs_[index] = 0;
    snprintf(diagnostics_.lastResult, sizeof(diagnostics_.lastResult),
             "%s: OK%s (%u ms)", jobName(job), changed ? " +data" : "",
             static_cast<unsigned>(durationMs));
  } else {
    if (failures_[index] < 255U) ++failures_[index];
    const uint32_t backoff = reactiveTlsRecovery
                                 ? Config::TLS_GUARD_RETRY_MS
                                 : failureBackoffMs(job, failures_[index]);
    nextAllowedMs_[index] = millis() + backoff;
    ++diagnostics_.failedJobs;
    if (reactiveTlsRecovery) {
      snprintf(diagnostics_.lastResult, sizeof(diagnostics_.lastResult),
               "%s: FAIL -> TLS recovery, retry >=%us (%u ms)", jobName(job),
               static_cast<unsigned>(backoff / 1000UL),
               static_cast<unsigned>(durationMs));
    } else {
      snprintf(diagnostics_.lastResult, sizeof(diagnostics_.lastResult),
               "%s: FAIL, retry >=%us (%u ms) %.36s", jobName(job),
               static_cast<unsigned>(backoff / 1000UL),
               static_cast<unsigned>(durationMs), result ? result : "");
    }
  }
  diagnostics_.pendingJobs = countBits(pendingMask_);
  xSemaphoreGive(stateMutex_);

  DebugLog::printf("NET worker: %s | %s | %u ms | heap=%u kB psram=%u kB\n",
                   jobName(job), success ? "OK" : "FAIL",
                   static_cast<unsigned>(durationMs),
                   static_cast<unsigned>(ESP.getFreeHeap() / 1024),
                   static_cast<unsigned>(ESP.getFreePsram() / 1024));
}

void NetworkWorker::taskEntry(void* context) {
  static_cast<NetworkWorker*>(context)->taskLoop();
}

void NetworkWorker::taskLoop() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
      Job job;
      if (!selectNextJob(job)) break;
      const uint32_t started = millis();
      bool changed = false;
      bool deferred = false;
      char result[96] = {};
      const bool success = execute(job, result, sizeof(result), changed, deferred);
      const uint32_t duration = millis() - started;
      finishJob(job, success, changed, deferred, duration, result);
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }
}

bool NetworkWorker::consumeLocalAdsb(AdsbService& target) {
  if (!resultMutex_ ||
      xSemaphoreTake(resultMutex_, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  const bool ready = pendingLocalReady_;
  if (ready) {
    target.applyLocalSnapshot(*pendingLocalAdsb_);
    pendingLocalReady_ = false;
  }
  xSemaphoreGive(resultMutex_);
  return ready;
}

bool NetworkWorker::consumeInternetAdsb(AdsbService& target) {
  if (!resultMutex_ ||
      xSemaphoreTake(resultMutex_, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  const bool ready = pendingInternetReady_;
  if (ready) {
    target.applyNetworkSnapshot(*pendingInternetAdsb_);
    pendingInternetReady_ = false;
  }
  xSemaphoreGive(resultMutex_);
  return ready;
}

bool NetworkWorker::consumeCurrentWeather(WeatherService& target) {
  if (!resultMutex_ ||
      xSemaphoreTake(resultMutex_, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  const bool ready = pendingCurrentReady_;
  if (ready) {
    target.applyCurrentSnapshot(pendingCurrent_);
    pendingCurrentReady_ = false;
  }
  xSemaphoreGive(resultMutex_);
  return ready;
}

bool NetworkWorker::consumeForecast(WeatherService& target) {
  if (!resultMutex_ ||
      xSemaphoreTake(resultMutex_, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  const bool ready = pendingForecastReady_;
  if (ready) {
    target.applyForecastSnapshot(pendingForecast_);
    pendingForecastReady_ = false;
  }
  xSemaphoreGive(resultMutex_);
  return ready;
}

bool NetworkWorker::consumeRadar(RadarService& target) {
  if (!resultMutex_ ||
      xSemaphoreTake(resultMutex_, pdMS_TO_TICKS(30)) != pdTRUE) return false;
  bool changed = false;
  if (pendingRadarReady_) {
    changed = target.applyRuntimeUpdate(pendingRadar_);
    pendingRadarReady_ = false;
  }
  xSemaphoreGive(resultMutex_);
  return changed;
}

NetworkWorker::Diagnostics NetworkWorker::diagnostics() const {
  Diagnostics copy;
  if (!stateMutex_ ||
      xSemaphoreTake(stateMutex_, pdMS_TO_TICKS(20)) != pdTRUE) return copy;
  copy = diagnostics_;
  copy.pendingJobs = countBits(pendingMask_);
  copy.taskAlive = task_ != nullptr;
  copy.running = runningJob_ >= 0;
  copy.paused = paused_;
  copy.tlsFreeInternal = static_cast<uint32_t>(
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  copy.tlsLargestInternalBlock = static_cast<uint32_t>(
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (tlsGuardLatched_ ||
      copy.tlsFreeInternal < Config::TLS_GUARD_CRITICAL_FREE_INTERNAL ||
      copy.tlsLargestInternalBlock < Config::TLS_GUARD_CRITICAL_LARGEST_BLOCK) {
    copy.tlsMemoryState = 2;
  } else if (copy.tlsLargestInternalBlock <
             Config::TLS_GUARD_WARNING_LARGEST_BLOCK) {
    copy.tlsMemoryState = 1;
  } else {
    copy.tlsMemoryState = 0;
  }
  copy.tlsGuardLowMemory = copy.tlsMemoryState >= 2;
  const uint32_t now = millis();
  const uint32_t internetDeadline =
      nextAllowedMs_[static_cast<uint8_t>(Job::AdsbInternet)];
  copy.adsbInternetNextRetryMs =
      deadlinePending(now, internetDeadline) ? internetDeadline - now : 0U;
  copy.adsbInternetFailures =
      failures_[static_cast<uint8_t>(Job::AdsbInternet)];
  copy.adsbLocalFailures = failures_[static_cast<uint8_t>(Job::AdsbLocal)];
  xSemaphoreGive(stateMutex_);
  return copy;
}
