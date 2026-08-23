#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "adsb_service.h"
#include "radar_service.h"
#include "weather_service.h"

class NetworkWorker {
 public:
  enum class Job : uint8_t {
    AdsbLocal = 0,
    WeatherCurrent,
    AdsbInternet,
    Radar,
    Forecast,
    Count
  };

  struct Diagnostics {
    bool running = false;
    bool paused = false;
    uint8_t pendingJobs = 0;
    uint32_t lastJobDurationMs = 0;
    uint32_t longestJobDurationMs = 0;
    uint32_t completedJobs = 0;
    uint32_t failedJobs = 0;
    uint32_t backoffSkips = 0;
    // TLS resource guard diagnostics. These values describe internal RAM
    // available before the most recent outbound HTTPS job.
    bool taskAlive = false;
    bool tlsGuardLowMemory = false;
    uint32_t tlsFreeInternal = 0;
    uint32_t tlsLargestInternalBlock = 0;
    uint32_t tlsDeferredJobs = 0;
    uint32_t tlsForcedAttempts = 0;
    uint32_t tlsRecoveryRequests = 0;
    uint8_t tlsConsecutiveDefers = 0;
    // Per-source ADS-B diagnostics. nextRetryMs is a remaining interval, not
    // an absolute millis() deadline, so it is directly useful in the web UI.
    uint8_t adsbInternetFailures = 0;
    uint32_t adsbInternetLastAttemptMs = 0;
    uint32_t adsbInternetLastSuccessMs = 0;
    uint32_t adsbInternetLastDurationMs = 0;
    uint32_t adsbInternetNextRetryMs = 0;
    int adsbInternetHttpCode = 0;
    size_t adsbInternetLastAircraftCount = 0;
    size_t adsbInternetLastMlatCount = 0;
    char adsbInternetSource[16] = "adsb.fi";
    char adsbInternetResult[96] = "adsb.fi ceka";
    uint8_t adsbLocalFailures = 0;
    uint32_t adsbLocalLastAttemptMs = 0;
    uint32_t adsbLocalLastSuccessMs = 0;
    uint32_t adsbLocalLastDurationMs = 0;
    char adsbLocalResult[96] = "ADS-B local ceka";
    char activeJob[24] = "idle";
    char lastResult[96] = "network worker ceka";
  };

  NetworkWorker();
  ~NetworkWorker();

  bool begin(RadarService* radar);
  void configure(const String& localAdsbUrl, bool localAdsbEnabled,
                 const String& wuApiKey, const String& wuStationId,
                 float latitude, float longitude);

  bool request(Job job, bool force = false);
  void requestAll(bool force = false);
  void setPaused(bool paused);
  bool busy() const;

  // Returns true once when the TLS guard asks the LightningMaps task to drop
  // its WSS transport briefly, freeing internal mbedTLS/socket memory.
  bool consumeTlsRecoveryRequest();

  // Consume completed data in the Arduino/main task. These methods do only
  // short memory copies / cache swaps; all DNS, TCP, TLS, HTTP and JSON work
  // has already completed in the background worker.
  bool consumeLocalAdsb(AdsbService& target);
  bool consumeInternetAdsb(AdsbService& target);
  bool consumeCurrentWeather(WeatherService& target);
  bool consumeForecast(WeatherService& target);
  bool consumeRadar(RadarService& target);

  Diagnostics diagnostics() const;
  static const char* jobName(Job job);

 private:
  struct WorkerConfig {
    String localAdsbUrl;
    bool localAdsbEnabled = false;
    String wuApiKey;
    String wuStationId;
    float latitude = 49.8175f;
    float longitude = 15.4730f;
  };

  static constexpr uint8_t kJobCount = static_cast<uint8_t>(Job::Count);

  static void taskEntry(void* context);
  void taskLoop();
  bool execute(Job job, char* result, size_t resultSize, bool& changed, bool& deferred);
  bool copyConfig(WorkerConfig& target);
  bool selectNextJob(Job& job);
  void finishJob(Job job, bool success, bool changed, bool deferred,
                 uint32_t durationMs, const char* result);
  uint32_t failureBackoffMs(Job job, uint8_t failures) const;
  bool tlsPreflight(Job job, char* result, size_t resultSize, bool& deferred);
  static bool jobUsesTls(Job job);
  static uint8_t jobMask(Job job) {
    return static_cast<uint8_t>(1U << static_cast<uint8_t>(job));
  }

  RadarService* radar_ = nullptr;
  AdsbService* localAdsbWorker_ = nullptr;
  AdsbService* internetAdsbWorker_ = nullptr;
  WeatherService* weatherWorker_ = nullptr;

  AircraftSnapshot* pendingLocalAdsb_ = nullptr;
  AircraftSnapshot* pendingInternetAdsb_ = nullptr;
  WeatherSnapshot pendingCurrent_;
  WeatherSnapshot pendingForecast_;
  RadarService::RuntimeFrameUpdate pendingRadar_;
  bool pendingLocalReady_ = false;
  bool pendingInternetReady_ = false;
  bool pendingCurrentReady_ = false;
  bool pendingForecastReady_ = false;
  bool pendingRadarReady_ = false;

  mutable SemaphoreHandle_t stateMutex_ = nullptr;
  SemaphoreHandle_t configMutex_ = nullptr;
  SemaphoreHandle_t resultMutex_ = nullptr;
  TaskHandle_t task_ = nullptr;

  WorkerConfig config_;
  uint8_t pendingMask_ = 0;
  int8_t runningJob_ = -1;
  bool paused_ = false;
  uint32_t nextAllowedMs_[kJobCount] = {};
  uint8_t failures_[kJobCount] = {};
  uint8_t tlsDefers_[kJobCount] = {};
  bool tlsRecoveryRequested_ = false;
  Diagnostics diagnostics_;
};
