#pragma once

#include <Arduino.h>
#include <FS.h>
#include <PNGdec.h>
#include <time.h>

#include "config.h"
#include "map_viewport.h"

class RadarService {
 public:
  RadarService();
  ~RadarService();

  bool begin();
  bool updateFrames();

  // Call after the RGB panel and compact animation cache are ready. Runtime
  // radar refreshes will then stay entirely in RAM/PSRAM and will not write
  // PNG files to LittleFS while the LCD DMA is active.
  void setDisplayActive(bool active) { displayActive_ = active; }

  // Decodes and scales all downloaded PNG files once. The compact 8-bit
  // overlays are then used by the animation, avoiding repeated PNG decoding
  // and large PSRAM bursts while the RGB LCD DMA is active.
  bool prepareAnimationCache(uint16_t width, uint16_t height);
  bool animationCacheReady() const {
    return cachedFrames_ == availableFrames_ && cachedFrames_ > 0;
  }

  bool renderFrame(uint8_t frameIndex, uint16_t* destination,
                   uint16_t width, uint16_t height,
                   const MapViewport& viewport);
  uint8_t frameCount() const { return availableFrames_; }
  const char* status() const { return status_; }
  const char* frameName(uint8_t index) const;
  bool frameTimeUtc(uint8_t index, time_t& timestamp) const;
  uint16_t sourceWidth() const { return sourceWidth_; }
  uint16_t sourceHeight() const { return sourceHeight_; }

 private:
  enum class DownloadResult : uint8_t { kOk, kNotFound, kFailed };

  static void* pngOpen(const char* filename, int32_t* size);
  static void pngClose(void* handle);
  static int32_t pngRead(PNGFILE* file, uint8_t* buffer, int32_t length);
  static int32_t pngSeek(PNGFILE* file, int32_t position);
  static void pngDraw(PNGDRAW* draw);

  bool loadCachedFrames();
  bool saveCachedNames();
  bool scanLatestFiles(String outNames[Config::RADAR_FRAME_COUNT],
                       uint8_t& count);
  bool stageFramesFromClock(String outNames[Config::RADAR_FRAME_COUNT],
                            uint8_t& count);
  bool stageFramesFromIndex(String outNames[Config::RADAR_FRAME_COUNT],
                            uint8_t& count);
  bool commitStagedFrames(
      const String names[Config::RADAR_FRAME_COUNT], uint8_t count,
      bool newestFirst);
  bool commitNewestFrame(const String& newestName, const String& stagedPath);
  DownloadResult downloadFile(const String& remoteName,
                              const String& localPath, int& httpCode);
  DownloadResult downloadFileToMemory(const String& remoteName,
                                      uint8_t*& data, size_t& dataSize,
                                      int& httpCode);
  bool updateNewestFrameInMemory(const String& newestName);
  bool updateFramesInMemory();
  bool decodeMemoryToOverlay(uint8_t* data, size_t dataSize,
                             uint8_t* overlay, uint16_t width,
                             uint16_t height);
  bool validatePngFile(const String& path) const;

  bool decodeFrame(uint8_t frameIndex);
  bool ensureDecodeBuffer(uint16_t width, uint16_t height);
  void copyDecodedToMap(uint16_t* destination, uint16_t width,
                        uint16_t height);
  void copyDecodedToCompactOverlay(uint8_t* destination, uint16_t width,
                                   uint16_t height);
  void compositeCompactOverlay(const uint8_t* overlay,
                               uint16_t* destination, uint16_t width,
                               uint16_t height,
                               const MapViewport& viewport) const;
  void clearAnimationCache();

  PNG png_;
  uint16_t* decoded_ = nullptr;
  size_t decodedCapacityPixels_ = 0;

  // Runtime RAM decode writes directly into one compact overlay, line by line.
  // It avoids a full-size decoded PNG buffer while the RGB panel is active.
  uint8_t* runtimeOverlayTarget_ = nullptr;
  uint16_t* runtimeLineBuffer_ = nullptr;
  uint16_t runtimeOverlayWidth_ = 0;
  uint16_t runtimeOverlayHeight_ = 0;
  int16_t runtimeSourceXByMapX_[Config::MAP_W] = {};
  int16_t runtimeSourceYByMapY_[Config::MAP_H] = {};

  // One RGB332 byte per output pixel. Six 600x444 overlays need about 1.52 MiB,
  // compared with more than 3 MiB for RGB565. Value zero means transparent.
  uint8_t* animationCache_[Config::RADAR_FRAME_COUNT] = {};
  uint16_t cacheWidth_ = 0;
  uint16_t cacheHeight_ = 0;
  uint8_t cachedFrames_ = 0;

  uint16_t sourceWidth_ = 0;
  uint16_t sourceHeight_ = 0;
  uint8_t availableFrames_ = 0;
  bool displayActive_ = false;
  String names_[Config::RADAR_FRAME_COUNT];
  char status_[112] = "Radar: cekam";

  static RadarService* active_;
  static File activeFile_;
};
