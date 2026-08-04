#include "radar_service.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include <time.h>

RadarService* RadarService::active_ = nullptr;
File RadarService::activeFile_;

namespace {
constexpr char kRadarPrefix[] = "pacz2gmaps3.z_max3d.";
constexpr char kCacheIndexPath[] = "/radar_names.txt";
constexpr time_t kValidEpoch = 1700000000;

bool isRadarName(const String& name) {
  if (!name.startsWith(kRadarPrefix) || !name.endsWith(".png")) return false;
  // pacz2gmaps3.z_max3d.YYYYMMDD.hhmm.0.png
  return name.length() == 39;
}

bool removeIfExists(const String& path) {
  return !LittleFS.exists(path) || LittleFS.remove(path);
}

bool removeIfExists(const char* path) {
  return !LittleFS.exists(path) || LittleFS.remove(path);
}

void sortNames(String names[], uint8_t count) {
  for (uint8_t i = 0; i < count; ++i) {
    for (uint8_t j = i + 1; j < count; ++j) {
      if (names[j] < names[i]) {
        String tmp = names[i];
        names[i] = names[j];
        names[j] = tmp;
      }
    }
  }
}

String radarNameForUtc(time_t timestamp) {
  struct tm utc {};
  gmtime_r(&timestamp, &utc);
  char name[64];
  snprintf(name, sizeof(name),
           "pacz2gmaps3.z_max3d.%04d%02d%02d.%02d%02d.0.png",
           utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour,
           utc.tm_min);
  return String(name);
}

float mercatorY(float latitudeDeg) {
  const float latitude = constrain(latitudeDeg, -85.0f, 85.0f) * DEG_TO_RAD;
  return logf(tanf(PI * 0.25f + latitude * 0.5f));
}

uint16_t blend565(uint16_t source, uint16_t destination, uint8_t alpha) {
  const uint16_t inv = 255U - alpha;
  const uint16_t sr = (source >> 11) & 0x1F;
  const uint16_t sg = (source >> 5) & 0x3F;
  const uint16_t sb = source & 0x1F;
  const uint16_t dr = (destination >> 11) & 0x1F;
  const uint16_t dg = (destination >> 5) & 0x3F;
  const uint16_t db = destination & 0x1F;
  const uint16_t r = (sr * alpha + dr * inv + 127U) / 255U;
  const uint16_t g = (sg * alpha + dg * inv + 127U) / 255U;
  const uint16_t b = (sb * alpha + db * inv + 127U) / 255U;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

bool isNearBlack(uint16_t pixel) {
  return ((pixel >> 11) & 0x1F) <= 1 && ((pixel >> 5) & 0x3F) <= 2 &&
         (pixel & 0x1F) <= 1;
}

uint8_t rgb565ToRgb332(uint16_t pixel) {
  const uint8_t red = static_cast<uint8_t>((pixel >> 13) & 0x07);
  const uint8_t green = static_cast<uint8_t>((pixel >> 8) & 0x07);
  const uint8_t blue = static_cast<uint8_t>((pixel >> 3) & 0x03);
  uint8_t packed = static_cast<uint8_t>((red << 5) | (green << 2) | blue);
  // Zero is reserved for a transparent radar pixel.
  if (packed == 0) packed = 1;
  return packed;
}

uint16_t rgb332ToRgb565(uint8_t pixel) {
  const uint8_t red3 = static_cast<uint8_t>((pixel >> 5) & 0x07);
  const uint8_t green3 = static_cast<uint8_t>((pixel >> 2) & 0x07);
  const uint8_t blue2 = static_cast<uint8_t>(pixel & 0x03);
  const uint16_t red5 = static_cast<uint16_t>((red3 << 2) | (red3 >> 1));
  const uint16_t green6 = static_cast<uint16_t>((green3 << 3) | green3);
  const uint16_t blue5 = static_cast<uint16_t>((blue2 << 3) |
                                                (blue2 << 1) |
                                                (blue2 >> 1));
  return static_cast<uint16_t>((red5 << 11) | (green6 << 5) | blue5);
}
}  // namespace

RadarService::RadarService() = default;

RadarService::~RadarService() {
  clearAnimationCache();
  if (decoded_) heap_caps_free(decoded_);
}

bool RadarService::begin() {
  if (!LittleFS.begin(true)) {
    snprintf(status_, sizeof(status_), "Radar: LittleFS chyba");
    return false;
  }

  active_ = this;
  if (loadCachedFrames()) {
    snprintf(status_, sizeof(status_), "Radar: cache %u/%u",
             static_cast<unsigned>(availableFrames_),
             static_cast<unsigned>(Config::RADAR_FRAME_COUNT));
  } else {
    snprintf(status_, sizeof(status_), "Radar: pripraven");
  }
  return true;
}

bool RadarService::loadCachedFrames() {
  File index = LittleFS.open(kCacheIndexPath, FILE_READ);
  if (!index) return false;

  uint8_t count = 0;
  while (index.available() && count < Config::RADAR_FRAME_COUNT) {
    String name = index.readStringUntil('\n');
    name.trim();
    const String localPath = "/radar_" + String(count) + ".png";
    if (!isRadarName(name) || !LittleFS.exists(localPath) ||
        !validatePngFile(localPath)) {
      break;
    }
    names_[count++] = name;
  }
  index.close();
  availableFrames_ = count;
  return count > 0;
}

bool RadarService::saveCachedNames() {
  File index = LittleFS.open(kCacheIndexPath, FILE_WRITE);
  if (!index) return false;
  for (uint8_t i = 0; i < availableFrames_; ++i) index.println(names_[i]);
  index.close();
  return true;
}

bool RadarService::scanLatestFiles(
    String outNames[Config::RADAR_FRAME_COUNT], uint8_t& count) {
  count = 0;
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.useHTTP10(true);
  http.setConnectTimeout(7000);
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, Config::RADAR_INDEX_URL)) {
    snprintf(status_, sizeof(status_), "Radar: nelze otevrit index");
    return false;
  }

  http.addHeader("User-Agent", "ESP32-CHMI-Radar/0.6");
  http.addHeader("Accept-Encoding", "identity");
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(status_, sizeof(status_), "Radar index HTTP %d", code);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  const uint32_t started = millis();
  while ((http.connected() || stream->available()) &&
         millis() - started < 45000UL) {
    if (!stream->available()) {
      if (!http.connected()) break;
      delay(2);
      continue;
    }

    const String line = stream->readStringUntil('\n');
    int searchFrom = 0;
    while (true) {
      const int start = line.indexOf(kRadarPrefix, searchFrom);
      if (start < 0) break;
      const int end = line.indexOf(".png", start);
      if (end < 0) break;
      const String candidate = line.substring(start, end + 4);
      searchFrom = end + 4;
      if (!isRadarName(candidate)) continue;

      bool duplicate = false;
      for (uint8_t i = 0; i < count; ++i) {
        if (outNames[i] == candidate) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) continue;

      if (count < Config::RADAR_FRAME_COUNT) {
        outNames[count++] = candidate;
        sortNames(outNames, count);
      } else if (candidate > outNames[0]) {
        outNames[0] = candidate;
        sortNames(outNames, count);
      }
    }
  }

  http.end();
  if (count == 0) {
    snprintf(status_, sizeof(status_), "Radar: prazdny index");
    return false;
  }
  Serial.printf("Radar index: %u files, newest %s\n",
                static_cast<unsigned>(count), outNames[count - 1].c_str());
  return true;
}

RadarService::DownloadResult RadarService::downloadFile(
    const String& remoteName, const String& localPath, int& httpCode) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(6500);
  http.setTimeout(14000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const String url = String(Config::RADAR_BASE_URL) + remoteName;
  if (!http.begin(client, url)) {
    httpCode = -1000;
    return DownloadResult::kFailed;
  }

  http.addHeader("User-Agent", "ESP32-CHMI-Radar/0.6");
  http.addHeader("Accept", "image/png");
  http.addHeader("Accept-Encoding", "identity");
  httpCode = http.GET();
  if (httpCode == HTTP_CODE_NOT_FOUND) {
    http.end();
    return DownloadResult::kNotFound;
  }
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    return DownloadResult::kFailed;
  }

  const String tempPath = localPath + ".part";
  removeIfExists(tempPath);
  File output = LittleFS.open(tempPath, FILE_WRITE);
  if (!output) {
    http.end();
    httpCode = -1001;
    return DownloadResult::kFailed;
  }

  WiFiClient* stream = http.getStreamPtr();
  int remaining = http.getSize();
  int written = 0;
  uint8_t transfer[2048];
  uint32_t lastData = millis();

  while ((http.connected() || stream->available()) &&
         (remaining > 0 || remaining == -1)) {
    const size_t available = stream->available();
    if (available == 0) {
      if (remaining == 0) break;
      if (millis() - lastData > 14000UL) break;
      delay(2);
      continue;
    }

    const size_t requested =
        available < sizeof(transfer) ? available : sizeof(transfer);
    const int received = stream->readBytes(transfer, requested);
    if (received <= 0) break;
    const size_t stored = output.write(transfer, static_cast<size_t>(received));
    if (stored != static_cast<size_t>(received)) {
      written = -1;
      break;
    }
    written += received;
    if (remaining > 0) remaining -= received;
    lastData = millis();

    // LittleFS and the RGB framebuffer share external-memory bandwidth on this
    // board. Short writes with a yield avoid starving the LCD DMA.
    delay(1);
  }

  output.close();
  http.end();

  if (written < 100 || (remaining > 0) || !validatePngFile(tempPath)) {
    removeIfExists(tempPath);
    httpCode = -1002;
    return DownloadResult::kFailed;
  }

  removeIfExists(localPath);
  if (!LittleFS.rename(tempPath, localPath)) {
    removeIfExists(tempPath);
    httpCode = -1003;
    return DownloadResult::kFailed;
  }
  return DownloadResult::kOk;
}


RadarService::DownloadResult RadarService::downloadFileToMemory(
    const String& remoteName, uint8_t*& data, size_t& dataSize,
    int& httpCode) {
  data = nullptr;
  dataSize = 0;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.useHTTP10(true);
  http.setConnectTimeout(6500);
  http.setTimeout(14000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const String url = String(Config::RADAR_BASE_URL) + remoteName;
  if (!http.begin(client, url)) {
    httpCode = -1100;
    return DownloadResult::kFailed;
  }

  http.addHeader("User-Agent", "ESP32-CHMI-Radar/0.7");
  http.addHeader("Accept", "image/png");
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("Connection", "close");
  httpCode = http.GET();
  if (httpCode == HTTP_CODE_NOT_FOUND) {
    http.end();
    client.stop();
    return DownloadResult::kNotFound;
  }
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    client.stop();
    return DownloadResult::kFailed;
  }

  constexpr size_t kMaximumPngBytes = 2U * 1024U * 1024U;
  const int declaredLength = http.getSize();
  if (declaredLength > static_cast<int>(kMaximumPngBytes)) {
    http.end();
    client.stop();
    httpCode = -1101;
    return DownloadResult::kFailed;
  }

  const size_t capacity = declaredLength > 0
                              ? static_cast<size_t>(declaredLength)
                              : kMaximumPngBytes;
  data = static_cast<uint8_t*>(heap_caps_malloc(
      capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!data) {
    http.end();
    client.stop();
    httpCode = -1102;
    return DownloadResult::kFailed;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint32_t lastData = millis();
  while ((http.connected() || stream->available()) && dataSize < capacity) {
    const size_t available = stream->available();
    if (available == 0) {
      if (!http.connected()) break;
      if (millis() - lastData > 14000UL) break;
      delay(2);
      continue;
    }

    size_t requested = available;
    if (requested > 512U) requested = 512U;
    if (requested > capacity - dataSize) requested = capacity - dataSize;
    const int received = stream->readBytes(data + dataSize, requested);
    if (received <= 0) break;
    dataSize += static_cast<size_t>(received);
    lastData = millis();

    // Keep the PSRAM burst short so the RGB DMA can refill its bounce buffer.
    delay(2);
  }

  http.end();
  client.stop();

  static constexpr uint8_t kPngSignature[8] = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  if (dataSize < 100U ||
      (declaredLength > 0 && dataSize != static_cast<size_t>(declaredLength)) ||
      memcmp(data, kPngSignature, sizeof(kPngSignature)) != 0) {
    heap_caps_free(data);
    data = nullptr;
    dataSize = 0;
    httpCode = -1103;
    return DownloadResult::kFailed;
  }

  Serial.printf("Radar RAM download: %s, %u bytes\n", remoteName.c_str(),
                static_cast<unsigned>(dataSize));
  return DownloadResult::kOk;
}

bool RadarService::decodeMemoryToOverlay(uint8_t* data, size_t dataSize,
                                         uint8_t* overlay, uint16_t width,
                                         uint16_t height) {
  if (!data || dataSize < 100U || !overlay || width == 0 || height == 0 ||
      width > Config::MAP_W || height > Config::MAP_H) {
    return false;
  }

  const int openResult =
      png_.openRAM(data, static_cast<int>(dataSize), pngDraw);
  if (openResult != PNG_SUCCESS) {
    snprintf(status_, sizeof(status_), "Radar RAM PNG open %d", openResult);
    return false;
  }

  sourceWidth_ = static_cast<uint16_t>(png_.getWidth());
  sourceHeight_ = static_cast<uint16_t>(png_.getHeight());
  if (sourceWidth_ == 0 || sourceHeight_ == 0 ||
      sourceWidth_ > Config::RADAR_SOURCE_MAX_W ||
      sourceHeight_ > Config::RADAR_SOURCE_MAX_H) {
    png_.close();
    snprintf(status_, sizeof(status_), "Radar RAM PNG rozmer %ux%u",
             static_cast<unsigned>(sourceWidth_),
             static_cast<unsigned>(sourceHeight_));
    return false;
  }

  runtimeLineBuffer_ = static_cast<uint16_t*>(heap_caps_malloc(
      static_cast<size_t>(sourceWidth_) * sizeof(uint16_t),
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!runtimeLineBuffer_) {
    png_.close();
    snprintf(status_, sizeof(status_), "Radar: chybi RAM pro radek PNG");
    return false;
  }

  runtimeOverlayTarget_ = overlay;
  runtimeOverlayWidth_ = width;
  runtimeOverlayHeight_ = height;
  for (uint16_t y = 0; y < height; ++y) {
    memset(runtimeOverlayTarget_ + static_cast<size_t>(y) * width, 0, width);
    if ((y & 0x07) == 0x07) delay(1);
  }

  const float mapLonSpan = Config::MAP_LON_RIGHT - Config::MAP_LON_LEFT;
  const float radarLonSpan = Config::RADAR_LON_RIGHT - Config::RADAR_LON_LEFT;
  const float mapMercTop = mercatorY(Config::MAP_LAT_TOP);
  const float mapMercBottom = mercatorY(Config::MAP_LAT_BOTTOM);
  const float radarMercTop = mercatorY(Config::RADAR_LAT_TOP);
  const float radarMercBottom = mercatorY(Config::RADAR_LAT_BOTTOM);

  for (uint16_t x = 0; x < width; ++x) {
    const float lon = Config::MAP_LON_LEFT +
                      (static_cast<float>(x) / (width - 1)) * mapLonSpan;
    runtimeSourceXByMapX_[x] = static_cast<int16_t>(lroundf(
        (lon - Config::RADAR_LON_LEFT) / radarLonSpan * (sourceWidth_ - 1)));
  }
  for (uint16_t y = 0; y < height; ++y) {
    const float fraction = static_cast<float>(y) / (height - 1);
    const float merc = mapMercTop - fraction * (mapMercTop - mapMercBottom);
    runtimeSourceYByMapY_[y] = static_cast<int16_t>(lroundf(
        (radarMercTop - merc) / (radarMercTop - radarMercBottom) *
        (sourceHeight_ - 1)));
  }

  const int decodeResult = png_.decode(nullptr, 0);
  png_.close();

  runtimeOverlayTarget_ = nullptr;
  runtimeOverlayWidth_ = 0;
  runtimeOverlayHeight_ = 0;
  heap_caps_free(runtimeLineBuffer_);
  runtimeLineBuffer_ = nullptr;

  if (decodeResult != PNG_SUCCESS) {
    snprintf(status_, sizeof(status_), "Radar RAM PNG decode %d",
             decodeResult);
    return false;
  }
  return true;
}

bool RadarService::updateNewestFrameInMemory(const String& newestName) {
  if (!animationCacheReady() || availableFrames_ != Config::RADAR_FRAME_COUNT ||
      cacheWidth_ == 0 || cacheHeight_ == 0) {
    snprintf(status_, sizeof(status_), "Radar: RAM cache neni pripraven");
    return false;
  }
  if (names_[availableFrames_ - 1] == newestName) {
    snprintf(status_, sizeof(status_), "Radar: aktualni %u/%u",
             static_cast<unsigned>(availableFrames_),
             static_cast<unsigned>(Config::RADAR_FRAME_COUNT));
    return false;
  }

  uint8_t* pngData = nullptr;
  size_t pngSize = 0;
  int code = 0;
  const DownloadResult result =
      downloadFileToMemory(newestName, pngData, pngSize, code);
  if (result != DownloadResult::kOk) {
    snprintf(status_, sizeof(status_), "Radar RAM HTTP %d", code);
    return false;
  }

  const size_t overlayBytes =
      static_cast<size_t>(cacheWidth_) * cacheHeight_;
  uint8_t* newOverlay = static_cast<uint8_t*>(heap_caps_malloc(
      overlayBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!newOverlay) {
    heap_caps_free(pngData);
    snprintf(status_, sizeof(status_), "Radar: malo PSRAM pro novy snimek");
    return false;
  }

  const bool decoded = decodeMemoryToOverlay(
      pngData, pngSize, newOverlay, cacheWidth_, cacheHeight_);
  heap_caps_free(pngData);
  if (!decoded) {
    heap_caps_free(newOverlay);
    return false;
  }

  heap_caps_free(animationCache_[0]);
  for (uint8_t i = 0; i + 1 < Config::RADAR_FRAME_COUNT; ++i) {
    animationCache_[i] = animationCache_[i + 1];
    names_[i] = names_[i + 1];
  }
  animationCache_[Config::RADAR_FRAME_COUNT - 1] = newOverlay;
  names_[Config::RADAR_FRAME_COUNT - 1] = newestName;
  cachedFrames_ = availableFrames_;

  snprintf(status_, sizeof(status_), "Radar: RAM novy snimek");
  Serial.printf("Radar RAM update complete: %s | free PSRAM %u kB\n",
                newestName.c_str(),
                static_cast<unsigned>(ESP.getFreePsram() / 1024));
  return true;
}

bool RadarService::updateFramesInMemory() {
  snprintf(status_, sizeof(status_), "Radar: kontrola bez zapisu");
  Serial.println("Radar: runtime RAM-only update; LittleFS will not be touched");

  String latest[Config::RADAR_FRAME_COUNT];
  uint8_t latestCount = 0;
  if (!scanLatestFiles(latest, latestCount) || latestCount == 0) {
    snprintf(status_, sizeof(status_), "Radar: index nedostupny, cache bezi");
    return false;
  }

  return updateNewestFrameInMemory(latest[latestCount - 1]);
}

bool RadarService::validatePngFile(const String& path) const {
  File file = LittleFS.open(path, FILE_READ);
  if (!file || file.size() < 100) {
    if (file) file.close();
    return false;
  }
  uint8_t signature[8] = {};
  const size_t read = file.read(signature, sizeof(signature));
  file.close();
  static constexpr uint8_t expected[8] = {0x89, 0x50, 0x4E, 0x47,
                                           0x0D, 0x0A, 0x1A, 0x0A};
  return read == sizeof(signature) && memcmp(signature, expected, 8) == 0;
}

bool RadarService::stageFramesFromClock(
    String outNames[Config::RADAR_FRAME_COUNT], uint8_t& count) {
  count = 0;
  time_t now = time(nullptr);
  if (now < kValidEpoch) {
    snprintf(status_, sizeof(status_), "Radar: cas NTP neni pripraven");
    return false;
  }

  // CHMI filenames use UTC and represent the end of a five-minute interval.
  // Start one interval behind the current rounded time to allow publication.
  const time_t rounded =
      (now / static_cast<time_t>(Config::RADAR_STEP_SECONDS)) *
          static_cast<time_t>(Config::RADAR_STEP_SECONDS) -
      static_cast<time_t>(Config::RADAR_STEP_SECONDS);

  uint8_t transportFailures = 0;
  for (uint8_t step = 0;
       step < Config::RADAR_LOOKBACK_STEPS &&
       count < Config::RADAR_FRAME_COUNT;
       ++step) {
    const time_t candidateTime =
        rounded - static_cast<time_t>(step) * Config::RADAR_STEP_SECONDS;
    const String name = radarNameForUtc(candidateTime);
    const String stagePath = "/radar_new_" + String(count) + ".png";
    int code = 0;
    const DownloadResult result = downloadFile(name, stagePath, code);
    if (result == DownloadResult::kOk) {
      outNames[count++] = name;  // newest first
      transportFailures = 0;
      Serial.printf("Radar downloaded: %s\n", name.c_str());
    } else if (result == DownloadResult::kFailed) {
      ++transportFailures;
      Serial.printf("Radar fetch failed: %s code %d\n", name.c_str(), code);
      if (transportFailures >= 3) {
        snprintf(status_, sizeof(status_), "Radar spojeni chyba %d", code);
        break;
      }
    }
    delay(2);
  }
  return count > 0;
}

bool RadarService::stageFramesFromIndex(
    String outNames[Config::RADAR_FRAME_COUNT], uint8_t& count) {
  String names[Config::RADAR_FRAME_COUNT];
  uint8_t found = 0;
  if (!scanLatestFiles(names, found)) {
    count = 0;
    return false;
  }

  count = 0;
  for (uint8_t i = 0; i < found; ++i) {
    const String stagePath = "/radar_new_" + String(count) + ".png";
    int code = 0;
    if (downloadFile(names[i], stagePath, code) == DownloadResult::kOk) {
      outNames[count++] = names[i];  // oldest first
      Serial.printf("Radar downloaded: %s\n", names[i].c_str());
    } else {
      snprintf(status_, sizeof(status_), "Radar soubor HTTP %d", code);
      return false;
    }
  }
  return count > 0;
}

bool RadarService::commitStagedFrames(
    const String stagedNames[Config::RADAR_FRAME_COUNT], uint8_t count,
    bool newestFirst) {
  if (count == 0) return false;

  // Keep the currently working cache until all newly downloaded files have
  // been moved successfully. This makes interrupted downloads harmless.
  for (uint8_t i = 0; i < Config::RADAR_FRAME_COUNT; ++i) {
    const String backup = "/radar_" + String(i) + ".old";
    const String current = "/radar_" + String(i) + ".png";
    removeIfExists(backup.c_str());
    if (LittleFS.exists(current.c_str())) {
      LittleFS.rename(current.c_str(), backup.c_str());
    }
  }

  bool success = true;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t sourceIndex = newestFirst ? count - 1 - i : i;
    const String staged = "/radar_new_" + String(sourceIndex) + ".png";
    const String target = "/radar_" + String(i) + ".png";
    removeIfExists(target.c_str());
    if (!LittleFS.rename(staged.c_str(), target.c_str())) {
      success = false;
      break;
    }
    names_[i] = stagedNames[sourceIndex];
  }

  if (!success) {
    for (uint8_t i = 0; i < Config::RADAR_FRAME_COUNT; ++i) {
      const String target = "/radar_" + String(i) + ".png";
      const String backup = "/radar_" + String(i) + ".old";
      removeIfExists(target.c_str());
      if (LittleFS.exists(backup.c_str())) {
        LittleFS.rename(backup.c_str(), target.c_str());
      }
    }
    loadCachedFrames();
    snprintf(status_, sizeof(status_), "Radar: chyba ulozeni");
    return false;
  }

  // Remove files left from a previously longer cache and all staging files.
  for (uint8_t i = 0; i < Config::RADAR_FRAME_COUNT; ++i) {
    const String target = "/radar_" + String(i) + ".png";
    const String backup = "/radar_" + String(i) + ".old";
    const String staged = "/radar_new_" + String(i) + ".png";
    const String part = staged + ".part";
    if (i >= count) removeIfExists(target.c_str());
    removeIfExists(backup.c_str());
    removeIfExists(staged.c_str());
    removeIfExists(part.c_str());
  }

  clearAnimationCache();
  availableFrames_ = count;
  saveCachedNames();
  snprintf(status_, sizeof(status_), "Radar: %u/%u snimku",
           static_cast<unsigned>(availableFrames_),
           static_cast<unsigned>(Config::RADAR_FRAME_COUNT));
  return true;
}

bool RadarService::commitNewestFrame(const String& newestName,
                                      const String& stagedPath) {
  if (availableFrames_ != Config::RADAR_FRAME_COUNT ||
      !validatePngFile(stagedPath)) {
    return false;
  }

  // Move the six existing files to temporary names. Only metadata is changed;
  // the five unchanged PNG payloads are not rewritten to flash.
  for (uint8_t i = 0; i < Config::RADAR_FRAME_COUNT; ++i) {
    const String current = "/radar_" + String(i) + ".png";
    const String old = "/radar_" + String(i) + ".old";
    const String restore = "/radar_restore_" + String(i) + ".png";
    removeIfExists(old.c_str());
    removeIfExists(restore.c_str());
    if (!LittleFS.exists(current.c_str()) ||
        !LittleFS.rename(current.c_str(), old.c_str())) {
      // Restore files already moved to .old before returning.
      for (uint8_t restoreIndex = 0; restoreIndex <= i; ++restoreIndex) {
        const String restoreTarget =
            "/radar_" + String(restoreIndex) + ".png";
        const String restoreOld =
            "/radar_" + String(restoreIndex) + ".old";
        if (!LittleFS.exists(restoreTarget.c_str()) &&
            LittleFS.exists(restoreOld.c_str())) {
          LittleFS.rename(restoreOld.c_str(), restoreTarget.c_str());
        }
      }
      snprintf(status_, sizeof(status_), "Radar: cache shift failed");
      loadCachedFrames();
      return false;
    }
  }

  bool success = true;
  for (uint8_t i = 0; i + 1 < Config::RADAR_FRAME_COUNT; ++i) {
    const String source = "/radar_" + String(i + 1) + ".old";
    const String target = "/radar_" + String(i) + ".png";
    if (!LittleFS.rename(source.c_str(), target.c_str())) {
      success = false;
      break;
    }
  }
  const String newestTarget =
      "/radar_" + String(Config::RADAR_FRAME_COUNT - 1) + ".png";
  if (success && !LittleFS.rename(stagedPath.c_str(), newestTarget.c_str())) {
    success = false;
  }

  if (!success) {
    // Reconstruct the original cache. Files already moved to their shifted
    // targets are first parked under restore names.
    for (uint8_t i = 0; i + 1 < Config::RADAR_FRAME_COUNT; ++i) {
      const String shifted = "/radar_" + String(i) + ".png";
      const String restore = "/radar_restore_" + String(i + 1) + ".png";
      if (LittleFS.exists(shifted.c_str())) {
        LittleFS.rename(shifted.c_str(), restore.c_str());
      }
    }
    removeIfExists(newestTarget.c_str());
    for (uint8_t i = 0; i < Config::RADAR_FRAME_COUNT; ++i) {
      const String target = "/radar_" + String(i) + ".png";
      const String old = "/radar_" + String(i) + ".old";
      const String restore = "/radar_restore_" + String(i) + ".png";
      removeIfExists(target.c_str());
      if (LittleFS.exists(old.c_str())) {
        LittleFS.rename(old.c_str(), target.c_str());
      } else if (LittleFS.exists(restore.c_str())) {
        LittleFS.rename(restore.c_str(), target.c_str());
      }
    }
    removeIfExists(stagedPath.c_str());
    loadCachedFrames();
    snprintf(status_, sizeof(status_), "Radar: shift rollback");
    return false;
  }

  removeIfExists("/radar_0.old");
  for (uint8_t i = 0; i < Config::RADAR_FRAME_COUNT; ++i) {
    const String old = "/radar_" + String(i) + ".old";
    const String restore = "/radar_restore_" + String(i) + ".png";
    removeIfExists(old.c_str());
    removeIfExists(restore.c_str());
  }

  for (uint8_t i = 0; i + 1 < Config::RADAR_FRAME_COUNT; ++i) {
    names_[i] = names_[i + 1];
  }
  names_[Config::RADAR_FRAME_COUNT - 1] = newestName;

  // Preserve the five already decoded overlays and leave only the newest one
  // to be decoded by prepareAnimationCache().
  if (animationCacheReady() && cacheWidth_ > 0 && cacheHeight_ > 0) {
    heap_caps_free(animationCache_[0]);
    for (uint8_t i = 0; i + 1 < Config::RADAR_FRAME_COUNT; ++i) {
      animationCache_[i] = animationCache_[i + 1];
    }
    animationCache_[Config::RADAR_FRAME_COUNT - 1] = nullptr;
    cachedFrames_ = Config::RADAR_FRAME_COUNT - 1;
  } else {
    clearAnimationCache();
  }

  saveCachedNames();
  snprintf(status_, sizeof(status_), "Radar: novy snimek 1/1");
  Serial.printf("Radar incremental update: %s\n", newestName.c_str());
  return true;
}

bool RadarService::updateFrames() {
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(status_, sizeof(status_), "Radar: WiFi offline");
    return false;
  }

  if (displayActive_) return updateFramesInMemory();

  snprintf(status_, sizeof(status_), "Radar: hledam snimky");
  Serial.println("Radar: reading CHMI directory index...");

  for (uint8_t i = 0; i < Config::RADAR_FRAME_COUNT; ++i) {
    const String staged = "/radar_new_" + String(i) + ".png";
    const String part = staged + ".part";
    removeIfExists(staged.c_str());
    removeIfExists(part.c_str());
  }
  removeIfExists("/radar_new_single.png");
  removeIfExists("/radar_new_single.png.part");

  String latest[Config::RADAR_FRAME_COUNT];
  uint8_t latestCount = 0;
  if (scanLatestFiles(latest, latestCount)) {
    bool identical = latestCount == availableFrames_;
    for (uint8_t i = 0; identical && i < latestCount; ++i) {
      identical = latest[i] == names_[i];
    }
    if (identical) {
      snprintf(status_, sizeof(status_), "Radar: aktualni %u/%u",
               static_cast<unsigned>(availableFrames_),
               static_cast<unsigned>(Config::RADAR_FRAME_COUNT));
      return false;
    }

    bool oneNewFrame = latestCount == Config::RADAR_FRAME_COUNT &&
                       availableFrames_ == Config::RADAR_FRAME_COUNT;
    for (uint8_t i = 0; oneNewFrame && i + 1 < Config::RADAR_FRAME_COUNT; ++i) {
      oneNewFrame = names_[i + 1] == latest[i];
    }

    if (oneNewFrame) {
      int code = 0;
      const String stagePath = "/radar_new_single.png";
      if (downloadFile(latest[Config::RADAR_FRAME_COUNT - 1], stagePath,
                       code) == DownloadResult::kOk &&
          commitNewestFrame(latest[Config::RADAR_FRAME_COUNT - 1], stagePath)) {
        return true;
      }
      Serial.printf("Radar incremental update failed, using full refresh (code %d)\n",
                    code);
    }

    uint8_t stagedCount = 0;
    String stagedNames[Config::RADAR_FRAME_COUNT];
    for (uint8_t i = 0; i < latestCount; ++i) {
      const String stagePath = "/radar_new_" + String(stagedCount) + ".png";
      int code = 0;
      if (downloadFile(latest[i], stagePath, code) != DownloadResult::kOk) {
        snprintf(status_, sizeof(status_), "Radar soubor HTTP %d", code);
        stagedCount = 0;
        break;
      }
      stagedNames[stagedCount++] = latest[i];
      Serial.printf("Radar downloaded: %s\n", latest[i].c_str());
    }
    if (stagedCount > 0) {
      return commitStagedFrames(stagedNames, stagedCount, false);
    }
  }

  Serial.println("Radar: directory index failed, trying UTC filenames...");
  String stagedNames[Config::RADAR_FRAME_COUNT];
  uint8_t stagedCount = 0;
  if (stageFramesFromClock(stagedNames, stagedCount)) {
    return commitStagedFrames(stagedNames, stagedCount, true);
  }

  if (availableFrames_ > 0) {
    snprintf(status_, sizeof(status_), "Radar: sit chyba, cache %u",
             static_cast<unsigned>(availableFrames_));
  } else {
    snprintf(status_, sizeof(status_), "Radar: data nedostupna");
  }
  return false;
}

const char* RadarService::frameName(uint8_t index) const {
  if (index >= availableFrames_) return "";
  return names_[index].c_str();
}

void* RadarService::pngOpen(const char* filename, int32_t* size) {
  activeFile_ = LittleFS.open(filename, FILE_READ);
  if (!activeFile_) return nullptr;
  *size = activeFile_.size();
  return &activeFile_;
}

void RadarService::pngClose(void* handle) {
  File* file = static_cast<File*>(handle);
  if (file) file->close();
}

int32_t RadarService::pngRead(PNGFILE* file, uint8_t* buffer, int32_t length) {
  File* source = static_cast<File*>(file->fHandle);
  if (!source) return 0;
  return source->read(buffer, length);
}

int32_t RadarService::pngSeek(PNGFILE* file, int32_t position) {
  File* source = static_cast<File*>(file->fHandle);
  if (!source) return 0;
  return source->seek(position) ? position : -1;
}

void RadarService::pngDraw(PNGDRAW* draw) {
  if (!active_ || !draw || draw->y < 0 ||
      draw->y >= active_->sourceHeight_ || draw->iWidth <= 0 ||
      draw->iWidth > active_->sourceWidth_) {
    return;
  }

  // Runtime refresh path: decode one source line into small internal RAM and
  // write only the matching rows of the compact RGB332 overlay. No full-size
  // decoded image and no LittleFS access are needed while the LCD is active.
  if (active_->runtimeOverlayTarget_ && active_->runtimeLineBuffer_) {
    active_->png_.getLineAsRGB565(draw, active_->runtimeLineBuffer_,
                                  PNG_RGB565_LITTLE_ENDIAN, 0x00000000);
    for (uint16_t mapY = 0; mapY < active_->runtimeOverlayHeight_; ++mapY) {
      if (active_->runtimeSourceYByMapY_[mapY] != draw->y) continue;
      uint8_t* outputRow =
          active_->runtimeOverlayTarget_ +
          static_cast<size_t>(mapY) * active_->runtimeOverlayWidth_;
      for (uint16_t mapX = 0; mapX < active_->runtimeOverlayWidth_; ++mapX) {
        const int sourceX = active_->runtimeSourceXByMapX_[mapX];
        if (sourceX < 0 || sourceX >= active_->sourceWidth_) continue;
        const uint16_t pixel = active_->runtimeLineBuffer_[sourceX];
        if (pixel == 0 || isNearBlack(pixel)) continue;
        outputRow[mapX] = rgb565ToRgb332(pixel);
      }
    }
    if ((draw->y & 0x07) == 0x07) delay(1);
    return;
  }

  if (!active_->decoded_) return;
  uint16_t* row = active_->decoded_ +
                  static_cast<size_t>(draw->y) * active_->sourceWidth_;
  active_->png_.getLineAsRGB565(draw, row, PNG_RGB565_LITTLE_ENDIAN,
                                0x00000000);

  // PNGdec can otherwise write a large image to PSRAM as one long burst.
  // Short pauses keep enough PSRAM bandwidth available for the RGB DMA.
  if ((draw->y & 0x07) == 0x07) delay(1);
}

bool RadarService::ensureDecodeBuffer(uint16_t width, uint16_t height) {
  if (width == 0 || height == 0 || width > Config::RADAR_SOURCE_MAX_W ||
      height > Config::RADAR_SOURCE_MAX_H) {
    snprintf(status_, sizeof(status_), "Radar PNG rozmer %ux%u nepodporovan",
             static_cast<unsigned>(width), static_cast<unsigned>(height));
    return false;
  }

  const size_t required = static_cast<size_t>(width) * height;
  if (decoded_ && decodedCapacityPixels_ >= required) return true;

  if (decoded_) {
    heap_caps_free(decoded_);
    decoded_ = nullptr;
    decodedCapacityPixels_ = 0;
  }
  decoded_ = static_cast<uint16_t*>(heap_caps_malloc(
      required * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!decoded_) {
    snprintf(status_, sizeof(status_), "Radar: malo PSRAM pro %ux%u",
             static_cast<unsigned>(width), static_cast<unsigned>(height));
    return false;
  }
  decodedCapacityPixels_ = required;
  Serial.printf("Radar decode buffer: %ux%u, %u bytes in PSRAM\n",
                static_cast<unsigned>(width), static_cast<unsigned>(height),
                static_cast<unsigned>(required * sizeof(uint16_t)));
  return true;
}

bool RadarService::decodeFrame(uint8_t frameIndex) {
  if (frameIndex >= availableFrames_) return false;

  const String localPath = "/radar_" + String(frameIndex) + ".png";
  const int openResult = png_.open(localPath.c_str(), pngOpen, pngClose,
                                   pngRead, pngSeek, pngDraw);
  if (openResult != PNG_SUCCESS) {
    snprintf(status_, sizeof(status_), "Radar PNG open %d", openResult);
    return false;
  }

  sourceWidth_ = static_cast<uint16_t>(png_.getWidth());
  sourceHeight_ = static_cast<uint16_t>(png_.getHeight());
  if (!ensureDecodeBuffer(sourceWidth_, sourceHeight_)) {
    png_.close();
    return false;
  }

  for (uint16_t y = 0; y < sourceHeight_; ++y) {
    memset(decoded_ + static_cast<size_t>(y) * sourceWidth_, 0,
           static_cast<size_t>(sourceWidth_) * sizeof(uint16_t));
    if ((y & 0x0F) == 0x0F) delay(1);
  }
  const int decodeResult = png_.decode(nullptr, 0);
  png_.close();
  if (decodeResult != PNG_SUCCESS) {
    snprintf(status_, sizeof(status_), "Radar PNG decode %d", decodeResult);
    return false;
  }
  return true;
}

void RadarService::copyDecodedToMap(uint16_t* destination, uint16_t width,
                                    uint16_t height) {
  const float mapLonSpan = Config::MAP_LON_RIGHT - Config::MAP_LON_LEFT;
  const float radarLonSpan = Config::RADAR_LON_RIGHT - Config::RADAR_LON_LEFT;
  const float mapMercTop = mercatorY(Config::MAP_LAT_TOP);
  const float mapMercBottom = mercatorY(Config::MAP_LAT_BOTTOM);
  const float radarMercTop = mercatorY(Config::RADAR_LAT_TOP);
  const float radarMercBottom = mercatorY(Config::RADAR_LAT_BOTTOM);

  for (uint16_t y = 0; y < height; ++y) {
    const float mapFraction = static_cast<float>(y) / (height - 1);
    const float merc = mapMercTop - mapFraction * (mapMercTop - mapMercBottom);
    const int sourceY = lroundf((radarMercTop - merc) /
                                (radarMercTop - radarMercBottom) *
                                (sourceHeight_ - 1));
    if (sourceY < 0 || sourceY >= sourceHeight_) continue;

    for (uint16_t x = 0; x < width; ++x) {
      const float lon = Config::MAP_LON_LEFT +
                        (static_cast<float>(x) / (width - 1)) * mapLonSpan;
      const int sourceX = lroundf((lon - Config::RADAR_LON_LEFT) /
                                  radarLonSpan * (sourceWidth_ - 1));
      if (sourceX < 0 || sourceX >= sourceWidth_) continue;

      const uint16_t radarPixel =
          decoded_[static_cast<size_t>(sourceY) * sourceWidth_ + sourceX];
      if (radarPixel == 0 || isNearBlack(radarPixel)) continue;
      const size_t destinationIndex = static_cast<size_t>(y) * width + x;
      destination[destinationIndex] =
          blend565(radarPixel, destination[destinationIndex], 224);
    }
  }
}

void RadarService::copyDecodedToCompactOverlay(uint8_t* destination,
                                                uint16_t width,
                                                uint16_t height) {
  if (!destination) return;

  const float mapLonSpan = Config::MAP_LON_RIGHT - Config::MAP_LON_LEFT;
  const float radarLonSpan = Config::RADAR_LON_RIGHT - Config::RADAR_LON_LEFT;
  const float mapMercTop = mercatorY(Config::MAP_LAT_TOP);
  const float mapMercBottom = mercatorY(Config::MAP_LAT_BOTTOM);
  const float radarMercTop = mercatorY(Config::RADAR_LAT_TOP);
  const float radarMercBottom = mercatorY(Config::RADAR_LAT_BOTTOM);

  // Build lookup tables once per decoded frame. This avoids more than 250,000
  // floating-point projection calculations during every animation step.
  int16_t sourceXByMapX[Config::MAP_W];
  int16_t sourceYByMapY[Config::MAP_H];

  for (uint16_t x = 0; x < width; ++x) {
    const float lon = Config::MAP_LON_LEFT +
                      (static_cast<float>(x) / (width - 1)) * mapLonSpan;
    sourceXByMapX[x] = static_cast<int16_t>(lroundf(
        (lon - Config::RADAR_LON_LEFT) / radarLonSpan * (sourceWidth_ - 1)));
  }
  for (uint16_t y = 0; y < height; ++y) {
    const float fraction = static_cast<float>(y) / (height - 1);
    const float merc = mapMercTop - fraction * (mapMercTop - mapMercBottom);
    sourceYByMapY[y] = static_cast<int16_t>(lroundf(
        (radarMercTop - merc) / (radarMercTop - radarMercBottom) *
        (sourceHeight_ - 1)));
  }

  for (uint16_t y = 0; y < height; ++y) {
    uint8_t* outputRow = destination + static_cast<size_t>(y) * width;
    memset(outputRow, 0, width);
    const int sourceY = sourceYByMapY[y];
    if (sourceY >= 0 && sourceY < sourceHeight_) {
      const uint16_t* sourceRow =
          decoded_ + static_cast<size_t>(sourceY) * sourceWidth_;
      for (uint16_t x = 0; x < width; ++x) {
        const int sourceX = sourceXByMapX[x];
        if (sourceX < 0 || sourceX >= sourceWidth_) continue;
        const uint16_t radarPixel = sourceRow[sourceX];
        if (radarPixel == 0 || isNearBlack(radarPixel)) continue;
        outputRow[x] = rgb565ToRgb332(radarPixel);
      }
    }
    if ((y & 0x07) == 0x07) delay(1);
  }
}

void RadarService::compositeCompactOverlay(const uint8_t* overlay,
                                            uint16_t* destination,
                                            uint16_t width, uint16_t height,
                                            const MapViewport& viewport) const {
  if (!overlay || !destination || width == 0 || height == 0) return;

  if (viewport.mode == MapZoomMode::Full && width == cacheWidth_ &&
      height == cacheHeight_) {
    for (uint16_t y = 0; y < height; ++y) {
      const size_t rowOffset = static_cast<size_t>(y) * width;
      for (uint16_t x = 0; x < width; ++x) {
        const size_t index = rowOffset + x;
        const uint8_t packed = overlay[index];
        if (packed == 0) continue;
        destination[index] =
            blend565(rgb332ToRgb565(packed), destination[index], 224);
      }
      if ((y & 0x0F) == 0x0F) delay(1);
    }
    return;
  }

  int16_t sourceXByMapX[Config::MAP_W];
  int16_t sourceYByMapY[Config::MAP_H];
  const float fullLonSpan = Config::MAP_LON_RIGHT - Config::MAP_LON_LEFT;
  const float viewLonSpan = viewport.lonRight - viewport.lonLeft;
  const float fullMercTop = mercatorY(Config::MAP_LAT_TOP);
  const float fullMercBottom = mercatorY(Config::MAP_LAT_BOTTOM);
  const float viewMercTop = mercatorY(viewport.latTop);
  const float viewMercBottom = mercatorY(viewport.latBottom);

  for (uint16_t x = 0; x < width; ++x) {
    const float fraction = width > 1 ? static_cast<float>(x) / (width - 1) : 0.0f;
    const float lon = viewport.lonLeft + fraction * viewLonSpan;
    sourceXByMapX[x] = static_cast<int16_t>(lroundf(
        (lon - Config::MAP_LON_LEFT) / fullLonSpan * (cacheWidth_ - 1)));
  }
  for (uint16_t y = 0; y < height; ++y) {
    const float fraction = height > 1 ? static_cast<float>(y) / (height - 1) : 0.0f;
    const float merc = viewMercTop - fraction * (viewMercTop - viewMercBottom);
    sourceYByMapY[y] = static_cast<int16_t>(lroundf(
        (fullMercTop - merc) / (fullMercTop - fullMercBottom) *
        (cacheHeight_ - 1)));
  }

  for (uint16_t y = 0; y < height; ++y) {
    const int sourceY = sourceYByMapY[y];
    if (sourceY < 0 || sourceY >= cacheHeight_) continue;
    const uint8_t* sourceRow =
        overlay + static_cast<size_t>(sourceY) * cacheWidth_;
    uint16_t* outputRow = destination + static_cast<size_t>(y) * width;
    for (uint16_t x = 0; x < width; ++x) {
      const int sourceX = sourceXByMapX[x];
      if (sourceX < 0 || sourceX >= cacheWidth_) continue;
      const uint8_t packed = sourceRow[sourceX];
      if (packed == 0) continue;
      outputRow[x] = blend565(rgb332ToRgb565(packed), outputRow[x], 224);
    }
    if ((y & 0x0F) == 0x0F) delay(1);
  }
}

void RadarService::clearAnimationCache() {
  for (uint8_t i = 0; i < Config::RADAR_FRAME_COUNT; ++i) {
    if (animationCache_[i]) {
      heap_caps_free(animationCache_[i]);
      animationCache_[i] = nullptr;
    }
  }
  cacheWidth_ = 0;
  cacheHeight_ = 0;
  cachedFrames_ = 0;
}

bool RadarService::prepareAnimationCache(uint16_t width, uint16_t height) {
  if (availableFrames_ == 0 || width == 0 || height == 0 ||
      width > Config::MAP_W || height > Config::MAP_H) {
    return false;
  }

  // Preserve shifted overlays after a normal one-frame radar update. A full
  // refresh or a layout change still starts with a clean cache.
  if (cacheWidth_ != width || cacheHeight_ != height) {
    clearAnimationCache();
  }

  const size_t overlayBytes = static_cast<size_t>(width) * height;
  cacheWidth_ = width;
  cacheHeight_ = height;
  snprintf(status_, sizeof(status_), "Radar: pripravuji animaci");

  uint8_t present = 0;
  for (uint8_t i = 0; i < availableFrames_; ++i) {
    if (animationCache_[i]) ++present;
  }
  Serial.printf("Radar: animation cache %u/%u present (%u bytes each)\n",
                static_cast<unsigned>(present),
                static_cast<unsigned>(availableFrames_),
                static_cast<unsigned>(overlayBytes));

  for (uint8_t i = 0; i < availableFrames_; ++i) {
    if (animationCache_[i]) continue;

    animationCache_[i] = static_cast<uint8_t*>(heap_caps_malloc(
        overlayBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!animationCache_[i]) {
      snprintf(status_, sizeof(status_), "Radar: malo PSRAM pro animaci");
      clearAnimationCache();
      return false;
    }

    if (!decodeFrame(i)) {
      clearAnimationCache();
      return false;
    }
    copyDecodedToCompactOverlay(animationCache_[i], width, height);
    Serial.printf("Radar cache: frame %u/%u ready\n",
                  static_cast<unsigned>(i + 1),
                  static_cast<unsigned>(availableFrames_));
    delay(12);
  }

  cachedFrames_ = 0;
  for (uint8_t i = 0; i < availableFrames_; ++i) {
    if (animationCache_[i]) ++cachedFrames_;
  }

  // The large PNG decode buffer is no longer needed during animation.
  if (decoded_) {
    heap_caps_free(decoded_);
    decoded_ = nullptr;
    decodedCapacityPixels_ = 0;
  }

  snprintf(status_, sizeof(status_), "Radar: %u/%u cache",
           static_cast<unsigned>(cachedFrames_),
           static_cast<unsigned>(Config::RADAR_FRAME_COUNT));
  Serial.printf("Radar compact cache ready, free PSRAM: %u kB\n",
                static_cast<unsigned>(ESP.getFreePsram() / 1024));
  return animationCacheReady();
}

bool RadarService::renderFrame(uint8_t frameIndex, uint16_t* destination,
                               uint16_t width, uint16_t height,
                               const MapViewport& viewport) {
  if (!destination || frameIndex >= availableFrames_) return false;

  if (animationCacheReady() && width == cacheWidth_ && height == cacheHeight_ &&
      animationCache_[frameIndex]) {
    compositeCompactOverlay(animationCache_[frameIndex], destination, width,
                            height, viewport);
    return true;
  }

  // Do not decode a full PNG during animation. Repeated large PSRAM writes
  // while the RGB DMA is active can desynchronize this panel. If the compact
  // cache cannot be built, keep the map visible and report the cache error.
  snprintf(status_, sizeof(status_), "Radar: animation cache unavailable");
  return false;
}

