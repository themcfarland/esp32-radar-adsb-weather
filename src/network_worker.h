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
  bool execute(Job job, char* result, size_t resultSize, bool& changed);
  bool copyConfig(WorkerConfig& target);
  bool selectNextJob(Job& job);
  void finishJob(Job job, bool success, bool changed,
                 uint32_t durationMs, const char* result);
  uint32_t failureBackoffMs(Job job, uint8_t failures) const;
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
  Diagnostics diagnostics_;
};
