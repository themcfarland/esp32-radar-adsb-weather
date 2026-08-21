#include "device_config.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include "config.h"
#include "debug_log.h"
#include "settings_defaults.h"
#include "version.h"

namespace {
constexpr char kPreferencesNamespace[] = "devicecfg";

bool usableDefault(const char* value) {
  if (!value || !value[0]) return false;
  return strstr(value, "YOUR_") == nullptr &&
         strstr(value, "CHANGE_ME") == nullptr;
}

String htmlEscape(const String& source) {
  String escaped;
  escaped.reserve(source.length() + 16);
  for (size_t i = 0; i < source.length(); ++i) {
    switch (source[i]) {
      case '&': escaped += F("&amp;"); break;
      case '<': escaped += F("&lt;"); break;
      case '>': escaped += F("&gt;"); break;
      case '"': escaped += F("&quot;"); break;
      case '\'': escaped += F("&#39;"); break;
      default: escaped += source[i]; break;
    }
  }
  return escaped;
}

String normalizedAircraftTarget(String value) {
  value.trim();
  String normalized;
  normalized.reserve(16);
  for (size_t i = 0; i < value.length() && normalized.length() < 16; ++i) {
    const char c = value[i];
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
        c == '~') {
      normalized += static_cast<char>(toupper(static_cast<unsigned char>(c)));
    }
  }
  return normalized;
}

bool urlLooksValid(const String& value) {
  return value.startsWith("http://") || value.startsWith("https://");
}

String boolJson(bool value) { return value ? String("true") : String("false"); }

String floatJson(float value, unsigned int decimals = 2) {
  return isfinite(value) ? String(value, decimals) : String("null");
}

String jsonEscape(const char* value) {
  String escaped;
  if (!value) return escaped;
  escaped.reserve(strlen(value) + 16);
  for (const char* p = value; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    switch (c) {
      case '\\': escaped += F("\\\\"); break;
      case '"': escaped += F("\\\""); break;
      case '\n': escaped += F("\\n"); break;
      case '\r': escaped += F("\\r"); break;
      case '\t': escaped += F("\\t"); break;
      default:
        if (c >= 0x20) escaped += static_cast<char>(c);
        break;
    }
  }
  return escaped;
}

int64_t ageMs(uint32_t now, uint32_t lastUpdate) {
  if (lastUpdate == 0) return -1;
  return static_cast<uint32_t>(now - lastUpdate);
}


String layerSummary(bool radar, bool lightning, bool adsb) {
  String summary;
  if (radar) summary += "radar";
  if (lightning) {
    if (!summary.isEmpty()) summary += " + ";
    summary += "blesky LightningMaps";
  }
  if (adsb) {
    if (!summary.isEmpty()) summary += " + ";
    summary += "ADS-B";
  }
  if (summary.isEmpty()) summary = "vsechny mapove vrstvy vypnuty";
  return summary;
}

constexpr const char* kBacklightDayNames[BACKLIGHT_DAY_COUNT] = {
    "Pondeli", "Utery", "Streda", "Ctvrtek",
    "Patek", "Sobota", "Nedele"};

bool parseTimeMinutes(String value, uint16_t& minutes) {
  value.trim();
  if (value.length() != 5 || value[2] != ':') return false;
  if (!isdigit(static_cast<unsigned char>(value[0])) ||
      !isdigit(static_cast<unsigned char>(value[1])) ||
      !isdigit(static_cast<unsigned char>(value[3])) ||
      !isdigit(static_cast<unsigned char>(value[4]))) {
    return false;
  }
  const int hour = (value[0] - '0') * 10 + (value[1] - '0');
  const int minute = (value[3] - '0') * 10 + (value[4] - '0');
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
  minutes = static_cast<uint16_t>(hour * 60 + minute);
  return true;
}

String formatTimeMinutes(uint16_t minutes) {
  minutes %= 24U * 60U;
  char text[6];
  snprintf(text, sizeof(text), "%02u:%02u",
           static_cast<unsigned>(minutes / 60U),
           static_cast<unsigned>(minutes % 60U));
  return String(text);
}

bool parseFloatValue(String value, float& result) {
  value.trim();
  if (value.isEmpty()) return false;
  char* end = nullptr;
  result = strtof(value.c_str(), &end);
  if (!end || end == value.c_str()) return false;
  while (*end && isspace(static_cast<unsigned char>(*end))) ++end;
  return *end == '\0' && isfinite(result);
}
}  // namespace

DeviceConfigService::DeviceConfigService() : server_(80) {}

void DeviceConfigService::load() {
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, true)) {
    DebugLog::println("Config: no saved NVS record; first-run AP will be used");
  } else {
    const bool initialized = preferences.getBool("initialized", false);
    if (initialized) {
      settings_.wifiSsid = preferences.getString("wifi_ssid", "");
      settings_.wifiPassword = preferences.getString("wifi_pass", "");
      settings_.wuApiKey = preferences.getString("wu_key", "");
      settings_.wuStationId = preferences.getString("wu_station", "");
      settings_.adsbUrl = preferences.getString("adsb_url", "");
      // v0.29.2 adds an explicit local ADS-B switch. Existing installations
      // that already have a local URL are migrated to enabled so their
      // behaviour does not unexpectedly change after OTA.
      settings_.localAdsbEnabled =
          preferences.isKey("adsb_local_on")
              ? preferences.getBool("adsb_local_on", false)
              : !settings_.adsbUrl.isEmpty();
      settings_.homeLat = preferences.getFloat("home_lat", Config::DEFAULT_HOME_LAT);
      settings_.homeLon = preferences.getFloat("home_lon", Config::DEFAULT_HOME_LON);
      if (!isfinite(settings_.homeLat) || !isfinite(settings_.homeLon) ||
          settings_.homeLat < Config::MAP_LAT_BOTTOM ||
          settings_.homeLat > Config::MAP_LAT_TOP ||
          settings_.homeLon < Config::MAP_LON_LEFT ||
          settings_.homeLon > Config::MAP_LON_RIGHT) {
        settings_.homeLat = Config::DEFAULT_HOME_LAT;
        settings_.homeLon = Config::DEFAULT_HOME_LON;
      }
      settings_.radarLayerEnabled = preferences.getBool("layer_radar", true);
      settings_.lightningLayerEnabled =
          preferences.getBool("layer_lightning", true);
      settings_.adsbLayerEnabled = preferences.getBool("layer_adsb", true);
      settings_.aircraftAlertEnabled = preferences.getBool("alert_on", false);

      // Migrate the single target used by v0.18.0 into slot 1.
      const String legacyTarget = preferences.getString("alert_id", "");
      settings_.aircraftAlertTargets[0] = normalizedAircraftTarget(
          preferences.getString("alert_id0", legacyTarget));
      settings_.aircraftAlertTargets[1] = normalizedAircraftTarget(
          preferences.getString("alert_id1", ""));
      settings_.aircraftAlertTargets[2] = normalizedAircraftTarget(
          preferences.getString("alert_id2", ""));
      settings_.backlightScheduleEnabled =
          preferences.getBool("bl_sched", true);
      settings_.barometerEnabled = preferences.getBool("baro_on", true);
      settings_.barometerAltitudeM = preferences.getFloat("baro_alt", 0.0f);
      settings_.barometerOffsetHpa = preferences.getFloat("baro_off", 0.0f);
      if (!isfinite(settings_.barometerAltitudeM) ||
          settings_.barometerAltitudeM < -500.0f ||
          settings_.barometerAltitudeM > 5000.0f) {
        settings_.barometerAltitudeM = 0.0f;
      }
      if (!isfinite(settings_.barometerOffsetHpa) ||
          settings_.barometerOffsetHpa < -50.0f ||
          settings_.barometerOffsetHpa > 50.0f) {
        settings_.barometerOffsetHpa = 0.0f;
      }
      for (size_t day = 0; day < BACKLIGHT_DAY_COUNT; ++day) {
        char key[16];
        snprintf(key, sizeof(key), "bl_en%u", static_cast<unsigned>(day));
        settings_.backlightDays[day].enabled =
            preferences.getBool(key, true);
        snprintf(key, sizeof(key), "bl_st%u", static_cast<unsigned>(day));
        settings_.backlightDays[day].startMinutes =
            preferences.getUShort(key, 6U * 60U);
        snprintf(key, sizeof(key), "bl_end%u", static_cast<unsigned>(day));
        settings_.backlightDays[day].endMinutes =
            preferences.getUShort(key, 23U * 60U);
        if (settings_.backlightDays[day].startMinutes >= 24U * 60U)
          settings_.backlightDays[day].startMinutes = 6U * 60U;
        if (settings_.backlightDays[day].endMinutes >= 24U * 60U)
          settings_.backlightDays[day].endMinutes = 23U * 60U;
      }

      DebugLog::printf("Config: restored for SSID '%s', layers=%s\n",
                       settings_.wifiSsid.c_str(),
                       layerSummary(settings_.radarLayerEnabled,
                                    settings_.lightningLayerEnabled,
                                    settings_.adsbLayerEnabled).c_str());
    }
    preferences.end();
  }

  // Compile-time values are optional defaults for weather and ADS-B only.
  // Wi-Fi intentionally remains empty on a new device so first setup always
  // starts in AP mode.
  if (settings_.wuApiKey.isEmpty() && usableDefault(WU_API_KEY)) {
    settings_.wuApiKey = WU_API_KEY;
  }
  if (settings_.wuStationId.isEmpty() && usableDefault(WU_STATION_ID)) {
    settings_.wuStationId = WU_STATION_ID;
  }
  if (settings_.adsbUrl.isEmpty() && usableDefault(ADSB_AIRCRAFT_URL)) {
    settings_.adsbUrl = ADSB_AIRCRAFT_URL;
    settings_.localAdsbEnabled = true;
  }
}

void DeviceConfigService::begin(const AircraftSnapshot* aircraftSnapshot,
                                const RuntimeDiagnostics* runtimeDiagnostics) {
  aircraftSnapshot_ = aircraftSnapshot;
  runtimeDiagnostics_ = runtimeDiagnostics;
  WiFi.persistent(false);
  WiFi.setHostname(Config::CONFIG_HOSTNAME);

  if (!settings_.wifiSsid.isEmpty() && connectStation(12000)) {
    DebugLog::printf("Config web on home network: %s\n", accessUrl().c_str());
  } else {
    startAccessPoint(settings_.wifiSsid.isEmpty()
                         ? "first setup"
                         : "saved Wi-Fi connection failed");
  }
  startWebServer();
}

bool DeviceConfigService::stationConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool DeviceConfigService::connectStation(uint32_t timeoutMs) {
  if (stationConnected()) return true;
  if (settings_.wifiSsid.isEmpty()) return false;

  const wifi_mode_t mode = portalActive_ ? WIFI_AP_STA : WIFI_STA;
  WiFi.mode(mode);
  WiFi.setSleep(false);
  WiFi.begin(settings_.wifiSsid.c_str(), settings_.wifiPassword.c_str());
  DebugLog::printf("WiFi: connecting to %s\n", settings_.wifiSsid.c_str());

  const uint32_t started = millis();
  while (!stationConnected() && millis() - started < timeoutMs) {
    delay(100);
  }

  if (!stationConnected()) {
    DebugLog::printf("WiFi: connection failed, status=%d\n",
                     static_cast<int>(WiFi.status()));
    return false;
  }

  if (portalActive_) stopAccessPoint();
  startMdns();
  configTzTime(Config::TZ_INFO, "pool.ntp.org", "time.cloudflare.com");
  DebugLog::printf("WiFi: connected, IP %s, config %s\n",
                   WiFi.localIP().toString().c_str(), accessUrl().c_str());
  return true;
}

void DeviceConfigService::ensureNetwork(uint32_t timeoutMs) {
  if (stationConnected()) return;
  if (settings_.wifiSsid.isEmpty()) {
    if (!portalActive_) startAccessPoint("Wi-Fi is not configured");
    return;
  }
  if (!connectStation(timeoutMs) && !portalActive_) {
    startAccessPoint("runtime Wi-Fi reconnect failed");
  }
}

void DeviceConfigService::startAccessPoint(const char* reason) {
  if (portalActive_) return;

  const uint16_t suffix = static_cast<uint16_t>(ESP.getEfuseMac() & 0xFFFFU);
  char ssid[40];
  snprintf(ssid, sizeof(ssid), "%s-%04X", Config::CONFIG_AP_PREFIX,
           static_cast<unsigned>(suffix));
  accessPointSsid_ = ssid;

  WiFi.mode(settings_.wifiSsid.isEmpty() ? WIFI_AP : WIFI_AP_STA);
  WiFi.setSleep(false);
  if (!WiFi.softAP(accessPointSsid_.c_str(), Config::CONFIG_AP_PASSWORD)) {
    DebugLog::println("Config AP: start failed");
    return;
  }

  portalActive_ = true;
  dnsServer_.start(53, "*", WiFi.softAPIP());
  DebugLog::printf("Config AP: %s | password %s | http://%s | %s\n",
                   accessPointSsid_.c_str(), Config::CONFIG_AP_PASSWORD,
                   WiFi.softAPIP().toString().c_str(),
                   reason ? reason : "configuration requested");
}

void DeviceConfigService::stopAccessPoint() {
  if (!portalActive_) return;
  dnsServer_.stop();
  WiFi.softAPdisconnect(false);
  portalActive_ = false;
  accessPointSsid_ = "";
  WiFi.mode(WIFI_STA);
}

void DeviceConfigService::startMdns() {
  if (!stationConnected()) return;
  if (mdnsStarted_) MDNS.end();
  mdnsStarted_ = MDNS.begin(Config::CONFIG_HOSTNAME);
  if (mdnsStarted_) {
    MDNS.addService("http", "tcp", 80);
    DebugLog::printf("mDNS: http://%s.local/\n", Config::CONFIG_HOSTNAME);
  }
}

void DeviceConfigService::startWebServer() {
  if (serverStarted_) return;

  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/save", HTTP_POST, [this]() { handleSave(); });
  server_.on("/factory-reset", HTTP_POST,
             [this]() { handleFactoryReset(); });
  server_.on("/reboot", HTTP_POST, [this]() { handleReboot(); });
  server_.on("/lcd-resync", HTTP_POST, [this]() { handleLcdResync(); });
  server_.on("/ota-prepare", HTTP_POST, [this]() { handleOtaPrepare(); });
  server_.on("/update", HTTP_POST,
             [this]() { handleOtaResult(); },
             [this]() { handleOtaUpload(); });
  server_.on("/api/status", HTTP_GET, [this]() { handleStatusJson(); });
  server_.on("/diagnostics", HTTP_GET, [this]() { handleDiagnostics(); });
  server_.on("/api/diagnostics", HTTP_GET,
             [this]() { handleDiagnosticsJson(); });

  // Common captive-portal detection URLs.
  server_.on("/generate_204", HTTP_ANY,
             [this]() { handleCaptivePortal(); });
  server_.on("/gen_204", HTTP_ANY, [this]() { handleCaptivePortal(); });
  server_.on("/hotspot-detect.html", HTTP_ANY,
             [this]() { handleCaptivePortal(); });
  server_.on("/connecttest.txt", HTTP_ANY,
             [this]() { handleCaptivePortal(); });
  server_.on("/ncsi.txt", HTTP_ANY, [this]() { handleCaptivePortal(); });
  server_.onNotFound([this]() { handleCaptivePortal(); });

  server_.begin();
  serverStarted_ = true;
  DebugLog::println("Config web server: started on port 80 for AP and STA");
}

void DeviceConfigService::loop() {
  // When a saved STA connection comes back while the fallback AP is active,
  // close the AP automatically. The web server remains reachable on the STA IP.
  if (portalActive_ && stationConnected()) {
    DebugLog::println("Config AP: station connection restored, stopping AP");
    stopAccessPoint();
    startMdns();
    configTzTime(Config::TZ_INFO, "pool.ntp.org", "time.cloudflare.com");
  }

  if (portalActive_) dnsServer_.processNextRequest();
  if (serverStarted_) server_.handleClient();

  // Any OTA failure raised from the synchronous multipart callback is shown
  // only after WebServer::handleClient() has returned. This keeps every LVGL
  // operation out of the flash-write path.
  if (otaDisplayFailurePending_) {
    otaDisplayFailurePending_ = false;
    if (otaDisplayCallback_) {
      otaDisplayCallback_(OtaDisplayEvent::Failure, otaFilename_.c_str(),
                          otaBytesWritten_, otaError_);
    }
  }

  // Render the OTA screen only after the lightweight preflight HTTP request
  // has been answered. Never perform LVGL work from the multipart upload
  // callback itself.
  if (otaPrepareDisplayPending_) {
    otaPrepareDisplayPending_ = false;
    if (otaDisplayCallback_) {
      otaDisplayCallback_(OtaDisplayEvent::Start, otaFilename_.c_str(), 0, 0);
    }
  }

  if (restartPending_ &&
      static_cast<int32_t>(millis() - restartAt_) >= 0) {
    DebugLog::println("Config: restarting device");
    DebugLog::flush();
    delay(80);
    ESP.restart();
  }
}

String DeviceConfigService::networkLabel() const {
  if (stationConnected()) {
    String label = "WiFi ";
    label += String(WiFi.RSSI());
    label += " dBm ";
    label += WiFi.localIP().toString();
    return label;
  }
  if (portalActive_) {
    String label = "AP ";
    label += accessPointSsid_;
    label += " 192.168.4.1";
    return label;
  }
  return "WiFi offline";
}

String DeviceConfigService::accessUrl() const {
  if (stationConnected()) {
    return String("http://") + WiFi.localIP().toString() + "/";
  }
  return "http://192.168.4.1/";
}

AircraftAlertConfig DeviceConfigService::alertConfig() const {
  AircraftAlertConfig result;
  result.enabled = settings_.aircraftAlertEnabled;
  for (size_t i = 0; i < AIRCRAFT_ALERT_SLOT_COUNT; ++i) {
    strlcpy(result.targets[i], settings_.aircraftAlertTargets[i].c_str(),
            sizeof(result.targets[i]));
  }
  return result;
}

bool DeviceConfigService::consumeRuntimeSettingsChanged() {
  const bool changed = runtimeSettingsChanged_;
  runtimeSettingsChanged_ = false;
  return changed;
}

bool DeviceConfigService::consumeLcdResyncRequested() {
  const bool requested = lcdResyncRequested_;
  lcdResyncRequested_ = false;
  return requested;
}

bool DeviceConfigService::saveSettings() {
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, false)) return false;
  bool ok = true;
  ok &= preferences.putBool("initialized", true) > 0;
  ok &= preferences.putString("wifi_ssid", settings_.wifiSsid) > 0;
  // Empty passwords are valid for an open network; do not use the byte count
  // as the success signal for this value.
  preferences.putString("wifi_pass", settings_.wifiPassword);
  preferences.putString("wu_key", settings_.wuApiKey);
  preferences.putString("wu_station", settings_.wuStationId);
  preferences.putString("adsb_url", settings_.adsbUrl);
  preferences.putBool("adsb_local_on", settings_.localAdsbEnabled);
  preferences.putFloat("home_lat", settings_.homeLat);
  preferences.putFloat("home_lon", settings_.homeLon);
  preferences.putBool("layer_radar", settings_.radarLayerEnabled);
  preferences.putBool("layer_lightning", settings_.lightningLayerEnabled);
  preferences.putBool("layer_adsb", settings_.adsbLayerEnabled);
  preferences.putBool("alert_on", settings_.aircraftAlertEnabled);
  preferences.putString("alert_id0", settings_.aircraftAlertTargets[0]);
  preferences.putString("alert_id1", settings_.aircraftAlertTargets[1]);
  preferences.putString("alert_id2", settings_.aircraftAlertTargets[2]);
  preferences.putBool("bl_sched", settings_.backlightScheduleEnabled);
  preferences.putBool("baro_on", settings_.barometerEnabled);
  preferences.putFloat("baro_alt", settings_.barometerAltitudeM);
  preferences.putFloat("baro_off", settings_.barometerOffsetHpa);
  for (size_t day = 0; day < BACKLIGHT_DAY_COUNT; ++day) {
    char key[16];
    snprintf(key, sizeof(key), "bl_en%u", static_cast<unsigned>(day));
    preferences.putBool(key, settings_.backlightDays[day].enabled);
    snprintf(key, sizeof(key), "bl_st%u", static_cast<unsigned>(day));
    preferences.putUShort(key, settings_.backlightDays[day].startMinutes);
    snprintf(key, sizeof(key), "bl_end%u", static_cast<unsigned>(day));
    preferences.putUShort(key, settings_.backlightDays[day].endMinutes);
  }
  preferences.end();
  return ok;
}

bool DeviceConfigService::alertTargetPresent(size_t slot) const {
  if (slot >= AIRCRAFT_ALERT_SLOT_COUNT ||
      !settings_.aircraftAlertEnabled ||
      settings_.aircraftAlertTargets[slot].isEmpty() || !aircraftSnapshot_ ||
      !aircraftSnapshot_->valid) {
    return false;
  }

  AircraftAlertConfig oneTarget;
  oneTarget.enabled = true;
  strlcpy(oneTarget.targets[0], settings_.aircraftAlertTargets[slot].c_str(),
          sizeof(oneTarget.targets[0]));
  for (size_t i = 0; i < aircraftSnapshot_->count; ++i) {
    if (aircraftMatchesAlert(aircraftSnapshot_->items[i], oneTarget)) return true;
  }
  return false;
}

String DeviceConfigService::buildPage() const {
  String page;
  page.reserve(32000);
  page += F("<!doctype html><html lang='cs'><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>Radar ADS-B nastaveni</title><style>");
  page += F("body{font-family:Arial,sans-serif;background:#07131c;color:#e8f1f5;margin:0;padding:18px}");
  page += F("main{max-width:820px;margin:auto}.card{background:#102433;border:1px solid #29495c;border-radius:12px;padding:16px;margin-bottom:14px}");
  page += F("h1{font-size:24px;margin:0 0 8px}h2{font-size:18px;margin:0 0 12px;color:#8bd5ff}");
  page += F("label{display:block;margin:12px 0 5px}input,select{width:100%;box-sizing:border-box;padding:11px;border-radius:8px;border:1px solid #456275;background:#071923;color:#fff}");
  page += F("input[type=checkbox]{width:auto;margin-right:8px}.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}.three{display:grid;grid-template-columns:1fr 1fr 1fr;gap:12px}.schedule{width:100%;border-collapse:collapse}.schedule th,.schedule td{padding:7px;border-bottom:1px solid #29495c;text-align:left}.schedule input[type=time]{min-width:120px}");
  page += F("button{background:#1976a8;color:white;border:0;border-radius:8px;padding:11px 16px;font-size:15px;cursor:pointer}.small{padding:8px 10px;margin:8px 5px 0 0}.nav{display:inline-block;background:#214f68;color:#fff;text-decoration:none;border-radius:8px;padding:9px 13px}.danger{background:#a83a3a}.muted{color:#a7bac5;font-size:13px}.ok{color:#6ee7a5}.warn{color:#ffd166}");
  page += F("code{background:#071923;padding:2px 5px;border-radius:4px}@media(max-width:700px){.row,.three{grid-template-columns:1fr}.schedule{font-size:13px}.schedule input[type=time]{min-width:92px;padding:8px}}</style></head><body><main>");
  page += F("<h1>Radar CR + ADS-B</h1><p class='muted'>Firmware ");
  page += htmlEscape(FW_VERSION);
  page += F("</p><p><a class='nav' href='/diagnostics'>Diagnostika zarizeni</a></p><section class='card'><h2>Stav a pristup</h2><p>Sit: <strong>");
  page += htmlEscape(networkLabel());
  page += F("</strong></p><p><a class='nav' href='/diagnostics'>Otevrit diagnostiku</a></p><p>Konfigurace v domaci siti: <code>");
  page += htmlEscape(accessUrl());
  page += F("</code> nebo <code>http://");
  page += htmlEscape(Config::CONFIG_HOSTNAME);
  page += F(".local/</code></p><p>Aktivni vrstvy: <strong>");
  page += layerSummary(settings_.radarLayerEnabled,
                       settings_.lightningLayerEnabled,
                       settings_.adsbLayerEnabled);
  page += F("</strong></p>");
  if (portalActive_) {
    page += F("<p class='warn'>Konfiguracni AP: <strong>");
    page += htmlEscape(accessPointSsid_);
    page += F("</strong>, heslo <code>");
    page += htmlEscape(Config::CONFIG_AP_PASSWORD);
    page += F("</code></p>");
  }
  if (settings_.aircraftAlertEnabled) {
    for (size_t slot = 0; slot < AIRCRAFT_ALERT_SLOT_COUNT; ++slot) {
      if (settings_.aircraftAlertTargets[slot].isEmpty()) continue;
      page += F("<p>Letoun ");
      page += String(slot + 1);
      page += F(": <strong>");
      page += htmlEscape(settings_.aircraftAlertTargets[slot]);
      page += alertTargetPresent(slot)
                  ? F("</strong> <span class='ok'>prave zachycen</span></p>")
                  : F("</strong> <span class='muted'>neni prave zachycen</span></p>");
    }
  } else {
    page += F("<p class='muted'>Zvyrazneni letounu je vypnute.</p>");
  }
  page += F("</section><form method='post' action='/save'>");

  page += F("<section class='card'><h2>Vrstvy mapy</h2><label><input type='checkbox' name='layer_radar' value='1'");
  if (settings_.radarLayerEnabled) page += F(" checked");
  page += F(">Zobrazit radarovou vrstvu</label><label><input type='checkbox' name='layer_lightning' value='1'");
  if (settings_.lightningLayerEnabled) page += F(" checked");
  page += F(">Zobrazit blesky LightningMaps LIVE</label><label><input type='checkbox' name='layer_adsb' value='1'");
  if (settings_.adsbLayerEnabled) page += F(" checked");
  page += F(">Zobrazit letouny ADS-B</label><p class='muted'>Vrstvy lze libovolne kombinovat. Blesky se prijimaji realtime jako plain JSON z LightningMaps pres WSS a kresli se nezavisle na CHMI radarove animaci.</p></section>");

  page += F("<section class='card'><h2>Plan podsviceni</h2><label><input type='checkbox' name='bl_schedule' value='1'");
  if (settings_.backlightScheduleEnabled) page += F(" checked");
  page += F(">Ridit podsviceni podle tydenniho planu</label><table class='schedule'><thead><tr><th>Den</th><th>Aktivni</th><th>Od</th><th>Do</th></tr></thead><tbody>");
  for (size_t day = 0; day < BACKLIGHT_DAY_COUNT; ++day) {
    page += F("<tr><td>");
    page += kBacklightDayNames[day];
    page += F("</td><td><input type='checkbox' name='bl_day_");
    page += String(day);
    page += F("' value='1'");
    if (settings_.backlightDays[day].enabled) page += F(" checked");
    page += F("></td><td><input type='time' name='bl_start_");
    page += String(day);
    page += F("' required value='");
    page += formatTimeMinutes(settings_.backlightDays[day].startMinutes);
    page += F("'></td><td><input type='time' name='bl_end_");
    page += String(day);
    page += F("' required value='");
    page += formatTimeMinutes(settings_.backlightDays[day].endMinutes);
    page += F("'></td></tr>");
  }
  page += F("</tbody></table><p class='muted'>Mimo aktivni interval se vypne pouze podsviceni; radar, ADS-B a web dale pracuji. Prvni dotyk displej probudi na 1 minutu a neprovede zadnou jinou akci. Interval muze prechazet pres pulnoc, napriklad 18:00-02:00. Stejny cas Od a Do znamena zapnuto po cely den. Vypnuty den je dostupny jen docasnym probuzenim dotykem.</p></section>");

  page += F("<section class='card'><h2>Zvyrazneni az tri letounu</h2><label><input type='checkbox' name='alert_enabled' value='1'");
  if (settings_.aircraftAlertEnabled) page += F(" checked");
  page += F(">Zapnout graficke zvyrazneni symbolu</label><div class='three'>");
  for (size_t slot = 0; slot < AIRCRAFT_ALERT_SLOT_COUNT; ++slot) {
    page += F("<div><label for='alert_target_");
    page += String(slot + 1);
    page += F("'>Letoun ");
    page += String(slot + 1);
    page += F(" - callsign nebo ICAO</label><input id='alert_target_");
    page += String(slot + 1);
    page += F("' name='alert_target_");
    page += String(slot + 1);
    page += F("' maxlength='16' value='");
    page += htmlEscape(settings_.aircraftAlertTargets[slot]);
    page += F("' placeholder='CSA123 / 4B1812'></div>");
  }
  page += F("</div><label for='aircraft_now'>Aktualne zachycene letouny</label><select id='aircraft_now'><option value=''>-- vyberte --</option>");
  if (aircraftSnapshot_ && aircraftSnapshot_->valid) {
    const size_t count = aircraftSnapshot_->count > 40 ? 40 : aircraftSnapshot_->count;
    for (size_t i = 0; i < count; ++i) {
      const Aircraft& aircraft = aircraftSnapshot_->items[i];
      const char* value = aircraft.flight[0] ? aircraft.flight : aircraft.hex;
      page += F("<option value='");
      page += htmlEscape(value);
      page += F("'>");
      if (aircraft.flight[0]) {
        page += htmlEscape(aircraft.flight);
        page += F(" | ICAO ");
      } else {
        page += F("ICAO ");
      }
      page += htmlEscape(aircraft.hex);
      if (!aircraft.fromLocal) page += F(" | adsb.fi");
      if (aircraft.mlatPosition) page += F(" | MLAT");
      if (aircraft.altitudeFt >= 0) {
        page += F(" | ");
        page += String(aircraft.altitudeFt);
        page += F(" ft");
      }
      page += F("</option>");
    }
  }
  page += F("</select><button type='button' class='small assign' data-slot='1'>Pouzit jako letoun 1</button><button type='button' class='small assign' data-slot='2'>Pouzit jako letoun 2</button><button type='button' class='small assign' data-slot='3'>Pouzit jako letoun 3</button><p class='muted'>Zvyrazni se pouze symbol a kruh kolem letounu. Nevznika zvuk ani vyskakovaci okno. Pri vypnute ADS-B vrstve se symboly nezobrazuji.</p></section>");

  page += F("<section class='card'><h2>Wi-Fi</h2><label for='wifi_ssid'>Nazev site (SSID)</label><input id='wifi_ssid' name='wifi_ssid' maxlength='32' required value='");
  page += htmlEscape(settings_.wifiSsid);
  page += F("'><label for='wifi_password'>Nove heslo</label><input id='wifi_password' name='wifi_password' type='password' maxlength='64' autocomplete='new-password' placeholder='Prazdne = zachovat stavajici heslo'><p class='muted'>Zmena SSID nebo zadani noveho hesla vyzaduje restart. Ostatni volby se pouziji okamzite.</p></section>");

  page += F("<section class='card'><h2>Poloha zarizeni</h2><div class='row'><div><label for='home_lat'>Zemepisna sirka</label><input id='home_lat' name='home_lat' type='number' min='48.30' max='51.30' step='0.00001' required value='");
  page += String(settings_.homeLat, 5);
  page += F("'></div><div><label for='home_lon'>Zemepisna delka</label><input id='home_lon' name='home_lon' type='number' min='11.70' max='19.00' step='0.00001' required value='");
  page += String(settings_.homeLon, 5);
  page += F("'></div></div><p class='muted'>HOME poloha se pouziva pro znacku na mape, 10km bleskovy alarm, Open-Meteo a astronomicke vypocty. Ceska casova zona CET/CEST je nastavena automaticky.</p></section>");

  page += F("<section class='card'><h2>Datove zdroje</h2><label><input type='checkbox' name='adsb_local_enabled' value='1'");
  if (settings_.localAdsbEnabled) page += F(" checked");
  page += F(">Pouzivat lokalni ADS-B prijimac</label><label for='adsb_url'>Lokalni ADS-B URL (aircraft.json) - volitelne</label><input id='adsb_url' name='adsb_url' maxlength='180' value='");
  page += htmlEscape(settings_.adsbUrl);
  page += F("' placeholder='http://192.168.1.100:8080/data/aircraft.json'><p class='muted'>Pri vypnuti se lokalni prijimac vubec nedotazuje; URL zustane ulozena a adsb.fi dal poskytuje provoz pro celou CR. Pri zapnuti se po 3 po sobe jdoucich chybach lokalniho prijimace automaticky pouzije 30s backoff.</p><div class='row'><div><label for='wu_station'>WU stanice - volitelne</label><input id='wu_station' name='wu_station' maxlength='20' value='");
  page += htmlEscape(settings_.wuStationId);
  page += F("'></div><div><label for='wu_key'>Novy WU API klic - volitelne</label><input id='wu_key' name='wu_key' type='password' maxlength='96' placeholder='Prazdne = zachovat ulozeny klic'></div></div><p class='muted'>Bez lokalniho prijimace se letadla nacitaji z adsb.fi pro celou CR. Pokud lokalni aircraft.json zadate, ma prioritu a adsb.fi doplni provoz mimo jeho dosah vcetne MLAT. Aktualni pocasi i predpoved funguji bez uctu pres Open-Meteo; Weather Underground je volitelny zdroj vlastni PWS. Prazdna WU stanice WU vypne.</p></section>");

  page += F("<section class='card'><h2>I2C barometr a Zambretti</h2><label><input type='checkbox' name='baro_enabled' value='1'");
  if (settings_.barometerEnabled) page += F(" checked");
  page += F(">Pouzit barometr BMP180</label><div class='row'><div><label for='baro_altitude'>Nadmorska vyska senzoru (m)</label><input id='baro_altitude' name='baro_altitude' type='number' min='-500' max='5000' step='0.1' value='");
  page += String(settings_.barometerAltitudeM, 1);
  page += F("'></div><div><label for='baro_offset'>Jemna korekce MSL tlaku (hPa)</label><input id='baro_offset' name='baro_offset' type='number' min='-50' max='50' step='0.1' value='");
  page += String(settings_.barometerOffsetHpa, 1);
  page += F("'></div></div><div style='margin-top:14px;padding:12px;border:1px solid #31566b;border-radius:9px;background:#0b1c27'><strong>Kalibrace podle referencniho tlaku</strong><div class='row'><div><label for='baro_reference'>Referencni tlak u hladiny more (hPa)</label><input id='baro_reference' type='number' min='850' max='1100' step='0.1' placeholder='napr. 1014.0'></div><div><label>Aktualni data</label><div id='baro_calibration_data' class='muted'>Nacitam tlak BMP180 a pocasi...</div></div></div><button type='button' id='baro_use_wu' class='small'>Pouzit aktualni tlak pocasi</button> <button type='button' id='baro_calculate' class='small'>Vypocitat nadmorskou vysku</button><p id='baro_calibration_result' class='muted'>Vypocet pouzije tlak primo z BMP180 a venkovni teplotu pouzivanou pro redukci. Vysledek se vlozi do pole nadmorske vysky; jemna korekce se vynuluje.</p></div><p class='muted'>Tlak BMP180 je tlak v miste senzoru. Hodnota kolem 974 hPa muze pri vysce priblizne 340 az 355 m odpovidat tlaku kolem 1014 hPa u hladiny more. Nadmorska vyska provadi hlavni fyzikalni prepocet; jemna korekce slouzi jen k doladeni o jednotky hPa. Trend za 3 hodiny se pocita z neupraveneho tlaku senzoru, takze kalibrace nezkresluje smer zmeny. BMP180 se hleda na 0x77 na sdilene I2C0: GPIO8 SDA / GPIO9 SCL.</p></section>");

  page += F("<button type='submit'>Ulozit nastaveni</button></form>");
  page += F("<section class='card' style='margin-top:14px'><h2>OTA aktualizace firmware</h2><form id='ota_form' method='post' action='/update' enctype='multipart/form-data'><label for='firmware_file'>Firmware .bin</label><input id='firmware_file' type='file' name='firmware' accept='.bin,application/octet-stream' required><button id='ota_button' type='submit' style='margin-top:12px'>Nahrat firmware pres OTA</button><p id='ota_state' class='muted'>Nahraje se firmware.bin do neaktivni OTA partition. Po uspesnem overeni se zarizeni automaticky restartuje do nove verze. Nastaveni v NVS zustanou zachovana.</p></form></section>");
  page += F("<section class='card'><h2>Servis</h2><form method='post' action='/lcd-resync' style='display:inline'><button type='submit'>Srovnat LCD</button></form> <form method='post' action='/reboot' style='display:inline'><button type='submit'>Restart</button></form> <form method='post' action='/factory-reset' style='display:inline'><button type='submit' class='danger'>Smazat nastaveni</button></form><p class='muted'>Srovnani LCD se provede pouze rucne; firmware nespousti periodicky restart RGB DMA.</p></section>");
  page += F("<script>const otaForm=document.getElementById('ota_form'),otaButton=document.getElementById('ota_button'),otaState=document.getElementById('ota_state'),otaFile=document.getElementById('firmware_file');if(otaForm)otaForm.addEventListener('submit',async(e)=>{e.preventDefault();const f=otaFile&&otaFile.files&&otaFile.files[0];if(!f)return;otaButton.disabled=true;otaButton.textContent='Pripravuji...';otaState.textContent='Pripravuji OTA obrazovku...';try{await fetch('/ota-prepare?name='+encodeURIComponent(f.name)+'&size='+encodeURIComponent(f.size),{method:'POST',cache:'no-store'});await new Promise(r=>setTimeout(r,1100));otaState.textContent='OTA zapisuje novy firmware. Displej muze byt behem zapisu zhasnuty. Nevypinejte zarizeni ani Wi-Fi.';otaButton.textContent='Nahravam...';otaForm.action='/update?size='+encodeURIComponent(f.size);otaForm.submit();}catch(err){otaButton.disabled=false;otaButton.textContent='Nahrat firmware';otaState.textContent='OTA priprava selhala: '+err;}});const s=document.getElementById('aircraft_now');document.querySelectorAll('.assign').forEach(b=>b.addEventListener('click',()=>{if(s.value)document.getElementById('alert_target_'+b.dataset.slot).value=s.value;}));let baroDiag=null;const calData=document.getElementById('baro_calibration_data');const calResult=document.getElementById('baro_calibration_result');async function loadBaroCalibration(){try{const r=await fetch('/api/diagnostics',{cache:'no-store'});if(!r.ok)throw Error(r.status);baroDiag=await r.json();if(!baroDiag.barometer_valid){calData.textContent='BMP180 zatim nema platne mereni';return;}const raw=baroDiag.barometer_raw_pressure_hpa;const temp=baroDiag.barometer_reduction_temperature_c;const wu=baroDiag.weather_pressure_hpa;calData.textContent='BMP180 '+raw.toFixed(1)+' hPa | T '+temp.toFixed(1)+' C | POCASI '+(wu===null?'--':wu.toFixed(1)+' hPa');if(wu!==null&&!document.getElementById('baro_reference').value)document.getElementById('baro_reference').value=wu.toFixed(1);}catch(e){calData.textContent='Diagnostika neni dostupna: '+e;}}document.getElementById('baro_use_wu').addEventListener('click',async()=>{if(!baroDiag)await loadBaroCalibration();if(!baroDiag||baroDiag.weather_pressure_hpa===null){calResult.textContent='Aktualni pocasi neposkytuje platny referencni tlak.';return;}document.getElementById('baro_reference').value=baroDiag.weather_pressure_hpa.toFixed(1);calResult.textContent='Referencni tlak byl prevzat z aktualniho zdroje pocasi.';});document.getElementById('baro_calculate').addEventListener('click',async()=>{if(!baroDiag)await loadBaroCalibration();if(!baroDiag||!baroDiag.barometer_valid){calResult.textContent='Nejprve je nutne platne mereni BMP180.';return;}const p=Number(baroDiag.barometer_raw_pressure_hpa),p0=Number(document.getElementById('baro_reference').value),t=Number(baroDiag.barometer_reduction_temperature_c);if(!Number.isFinite(p0)||p0<850||p0>1100||!Number.isFinite(p)||p<=0){calResult.textContent='Zadejte platny referencni tlak 850 az 1100 hPa.';return;}const h=((Math.pow(p0/p,1/5.257)-1)*(t+273.15))/0.0065;if(!Number.isFinite(h)||h<-500||h>5000){calResult.textContent='Z techto hodnot nelze vypocitat platnou vysku.';return;}document.getElementById('baro_altitude').value=h.toFixed(1);document.getElementById('baro_offset').value='0.0';calResult.textContent=p.toFixed(1)+' hPa -> '+p0.toFixed(1)+' hPa pri '+t.toFixed(1)+' C: navrzena vyska '+h.toFixed(1)+' m. Stisknete Ulozit nastaveni.';});loadBaroCalibration();</script></main></body></html>");
  return page;
}

String DeviceConfigService::buildDiagnosticsPage() const {
  String page;
  page.reserve(17000);
  page += F("<!doctype html><html lang='cs'><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>Diagnostika Radar ADS-B</title><style>");
  page += F("body{font-family:Arial,sans-serif;background:#07131c;color:#e8f1f5;margin:0;padding:18px}main{max-width:980px;margin:auto}");
  page += F("h1{margin:0 0 6px;font-size:25px}h2{margin:0 0 10px;color:#8bd5ff;font-size:18px}.muted{color:#9fb4c1}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.card{background:#102433;border:1px solid #29495c;border-radius:12px;padding:16px}.wide{grid-column:1/-1}table{width:100%;border-collapse:collapse}td{padding:7px 4px;border-bottom:1px solid #203c4d;vertical-align:top}td:first-child{color:#a9bdc8;width:43%}.value{font-family:Consolas,monospace;overflow-wrap:anywhere}.ok{color:#6ee7a5}.warn{color:#ffd166}.bad{color:#ff8b8b}.nav{display:inline-block;background:#1976a8;color:#fff;text-decoration:none;border-radius:8px;padding:10px 14px;margin:8px 8px 14px 0}button{background:#214f68;color:#fff;border:0;border-radius:8px;padding:10px 14px;cursor:pointer}@media(max-width:760px){.grid{grid-template-columns:1fr}.wide{grid-column:auto}}</style></head><body><main>");
  page += F("<h1>Diagnostika zarizeni</h1><p class='muted'>Automaticka obnova kazdych 5 sekund</p><a class='nav' href='/'>Nastaveni</a><a class='nav' href='/api/diagnostics'>JSON API</a><button onclick='loadData()'>Obnovit</button><div class='grid'>");
  page += F("<section class='card'><h2>System a cas</h2><table><tr><td>Firmware</td><td id='firmware' class='value'>--</td></tr><tr><td>Mistni cas</td><td id='local_datetime' class='value'>--</td></tr><tr><td>Casova zona</td><td id='timezone' class='value'>--</td></tr><tr><td>NTP synchronizace</td><td id='time_sync'>--</td></tr><tr><td>Doba behu</td><td id='uptime'>--</td></tr><tr><td>CPU</td><td id='cpu'>--</td></tr><tr><td>Flash</td><td id='flash'>--</td></tr><tr><td>Duvod restartu</td><td id='reset_reason'>--</td></tr></table></section>");
  page += F("<section class='card'><h2>Pamet</h2><table><tr><td>Volna heap</td><td id='heap_free'>--</td></tr><tr><td>Minimum heap</td><td id='heap_min'>--</td></tr><tr><td>Nejvetsi heap blok</td><td id='heap_largest'>--</td></tr><tr><td>Volna PSRAM</td><td id='psram_free'>--</td></tr><tr><td>Minimum PSRAM</td><td id='psram_min'>--</td></tr><tr><td>Nejvetsi PSRAM blok</td><td id='psram_largest'>--</td></tr></table></section>");
  page += F("<section class='card'><h2>Sit</h2><table><tr><td>Rezim</td><td id='network_mode'>--</td></tr><tr><td>SSID</td><td id='ssid' class='value'>--</td></tr><tr><td>IP adresa</td><td id='ip' class='value'>--</td></tr><tr><td>Signal</td><td id='rssi'>--</td></tr><tr><td>Hostname</td><td id='hostname' class='value'>--</td></tr><tr><td>Konfiguracni AP</td><td id='portal'>--</td></tr></table></section>");
  page += F("<section class='card'><h2>Displej a mapa</h2><table><tr><td>Rozliseni</td><td>800 x 480</td></tr><tr><td>Podsviceni</td><td id='backlight_state'>--</td></tr><tr><td>Tydenni plan</td><td id='backlight_schedule'>--</td></tr><tr><td>Docasne probuzeni</td><td id='backlight_wake'>--</td></tr><tr><td>Vyrez mapy</td><td id='map_view'>--</td></tr><tr><td>Prekresleni mapy</td><td id='map_redraws'>--</td></tr><tr><td>Posledni kresleni</td><td id='map_duration'>--</td></tr><tr><td>Srovnani RGB DMA</td><td id='lcd_resyncs'>--</td></tr><tr><td>Od posledniho srovnani</td><td id='lcd_age'>--</td></tr></table></section>");
  page += F("<section class='card wide'><h2>Datove zdroje</h2><table><tr><td>Radar</td><td id='radar_status' class='value'>--</td></tr><tr><td>Radarove snimky</td><td id='radar_frames'>--</td></tr><tr><td>Stari aktualizace radaru</td><td id='radar_age'>--</td></tr><tr><td>Blesky LightningMaps</td><td id='lightning_status' class='value'>--</td></tr><tr><td>Blesky v bufferu</td><td id='lightning_strikes'>--</td></tr><tr><td>Stari aktualizace blesku</td><td id='lightning_age'>--</td></tr><tr><td>ADS-B</td><td id='adsb_status' class='value'>--</td></tr><tr><td>Pocet letounu</td><td id='aircraft_count'>--</td></tr><tr><td>Lokalni / sit / MLAT</td><td id='aircraft_sources'>--</td></tr><tr><td>Stari ADS-B dat</td><td id='adsb_age'>--</td></tr><tr><td>Aktualni pocasi</td><td id='weather_status' class='value'>--</td></tr><tr><td>Stari pocasi</td><td id='weather_age'>--</td></tr><tr><td>Predpoved</td><td id='forecast_status' class='value'>--</td></tr><tr><td>Stari predpovedi</td><td id='forecast_age'>--</td></tr><tr><td>Astronomie</td><td id='astronomy_status' class='value'>--</td></tr><tr><td>Stari astronomie</td><td id='astronomy_age'>--</td></tr></table></section>");
  page += F("<section class='card wide'><h2>Barometr a Zambretti</h2><table><tr><td>Povoleno</td><td id='barometer_enabled'>--</td></tr><tr><td>Senzor</td><td id='barometer_sensor' class='value'>--</td></tr><tr><td>Stav</td><td id='barometer_status' class='value'>--</td></tr><tr><td>Nastavena vyska / korekce</td><td id='barometer_calibration'>--</td></tr><tr><td>Referencni tlak pocasi</td><td id='weather_pressure'>--</td></tr><tr><td>Tlak u hladiny more</td><td id='barometer_pressure'>--</td></tr><tr><td>Tlak senzoru</td><td id='barometer_raw_pressure'>--</td></tr><tr><td>Teplota senzoru</td><td id='barometer_temperature'>--</td></tr><tr><td>Teplota pro prepocet</td><td id='barometer_reduction_temperature'>--</td></tr><tr><td>Zdroj teploty</td><td id='barometer_reduction_source'>--</td></tr><tr><td>Venkovni prumer / vzorky</td><td id='wu_temperature_average'>--</td></tr><tr><td>Stari posledni venkovni teploty</td><td id='wu_temperature_age'>--</td></tr><tr><td>Zmena za 3 h</td><td id='barometer_delta'>--</td></tr><tr><td>Trend tlaku</td><td id='barometer_trend'>--</td></tr><tr><td>Zambretti kod</td><td id='zambretti_code'>--</td></tr><tr><td>Zambretti trend</td><td id='zambretti_trend'>--</td></tr><tr><td>Zambretti predpoved</td><td id='barometer_forecast'>--</td></tr><tr><td>Korekce vetrem</td><td id='zambretti_wind'>--</td></tr><tr><td>Sezonni korekce</td><td id='zambretti_season'>--</td></tr><tr><td>Body historie</td><td id='barometer_history'>--</td></tr><tr><td>Stari mereni</td><td id='barometer_age'>--</td></tr></table></section>");
  page += F("<section class='card wide'><h2>Nastaveni zobrazeni</h2><table><tr><td>Vrstvy</td><td id='layers'>--</td></tr><tr><td>Zvyrazneni letounu</td><td id='alerts'>--</td></tr></table></section></div><p id='refresh_state' class='muted'>Nacitam...</p>");
  page += F("<script>const $=id=>document.getElementById(id);const kb=v=>Math.round(v/1024)+' kB';const age=v=>v<0?'dosud neprovedeno':v<1000?v+' ms':v<60000?Math.round(v/1000)+' s':v<3600000?Math.round(v/60000)+' min':(v/3600000).toFixed(1)+' h';const up=v=>{let s=Math.floor(v/1000),d=Math.floor(s/86400);s%=86400;let h=Math.floor(s/3600);s%=3600;let m=Math.floor(s/60);return (d?d+' d ':'')+h+' h '+m+' min'};const yes=(v,a='ano',n='ne')=>v?'<span class=ok>'+a+'</span>':'<span class=warn>'+n+'</span>';async function loadData(){try{const r=await fetch('/api/diagnostics',{cache:'no-store'});if(!r.ok)throw Error(r.status);const d=await r.json();$('firmware').textContent=d.firmware;$('local_datetime').textContent=d.local_date+' '+d.local_time;$('timezone').textContent=d.timezone+' (automaticky CET/CEST)';$('time_sync').innerHTML=yes(d.time_synchronized,'synchronizovano','ceka na NTP');$('uptime').textContent=up(d.uptime_ms);$('cpu').textContent=d.cpu_mhz+' MHz / '+d.cpu_cores+' jadra';$('flash').textContent=kb(d.flash_bytes);$('reset_reason').textContent=d.reset_reason;$('heap_free').textContent=kb(d.heap_free);$('heap_min').textContent=kb(d.heap_min);$('heap_largest').textContent=kb(d.heap_largest);$('psram_free').textContent=kb(d.psram_free);$('psram_min').textContent=kb(d.psram_min);$('psram_largest').textContent=kb(d.psram_largest);$('network_mode').textContent=d.network_mode;$('ssid').textContent=d.ssid||'--';$('ip').textContent=d.ip;$('rssi').textContent=d.wifi_connected?d.rssi_dbm+' dBm':'--';$('hostname').textContent=d.hostname+'.local';$('portal').innerHTML=yes(d.portal_active,'aktivni','vypnuty');$('backlight_state').innerHTML=yes(d.backlight_on,'zapnuto','vypnuto');$('backlight_schedule').textContent=d.backlight_schedule_enabled?(d.backlight_window_active?'aktivni interval':'mimo aktivni interval'):'plan vypnut';$('backlight_wake').textContent=d.backlight_temporary_wake?Math.ceil(d.backlight_wake_remaining_ms/1000)+' s':'neaktivni';$('map_view').textContent=d.map_view;$('map_redraws').textContent=d.map_redraw_count;$('map_duration').textContent=d.last_map_redraw_ms+' ms';$('lcd_resyncs').textContent=d.lcd_resync_count;$('lcd_age').textContent=age(d.lcd_resync_age_ms);$('radar_status').textContent=d.radar_status;$('radar_frames').textContent=(d.radar_frame_count?(d.current_radar_frame+1)+' / '+d.radar_frame_count:'0 / 0')+' | cache '+(d.radar_cache_ready?'OK':'nepripravena');$('radar_age').textContent=age(d.radar_age_ms);$('lightning_status').textContent=d.lightning_status+' | '+(d.lightning_ready?'data OK':'bez dat');$('lightning_strikes').textContent=d.lightning_strike_count+' | realtime 20 min trail';$('lightning_age').textContent=age(d.lightning_age_ms);$('adsb_status').textContent=d.adsb_status;$('aircraft_count').textContent=d.aircraft_count;$('aircraft_sources').textContent=d.aircraft_local+' / '+d.aircraft_adsbfi+' / '+d.aircraft_mlat;$('adsb_age').textContent=age(d.adsb_age_ms);$('weather_status').textContent=d.weather_status+' | data '+(d.current_weather_valid?'OK':'chybi');$('weather_age').textContent=age(d.weather_age_ms);$('forecast_status').textContent=d.forecast_product+' | '+d.forecast_slot_count+' karet | '+(d.forecast_valid?'OK':'chyba');$('forecast_age').textContent=age(d.forecast_age_ms);$('astronomy_status').textContent=d.astronomy_status+' | '+(d.astronomy_valid?'OK':'chyba');$('astronomy_age').textContent=age(d.astronomy_age_ms);$('barometer_enabled').innerHTML=yes(d.barometer_enabled,'zapnut','vypnut');$('barometer_sensor').textContent=d.barometer_sensor+(d.barometer_address?' | 0x'+d.barometer_address.toString(16).toUpperCase():'');$('barometer_status').textContent=d.barometer_status;$('barometer_calibration').textContent=d.barometer_altitude_m.toFixed(1)+' m / '+(d.barometer_offset_hpa>=0?'+':'')+d.barometer_offset_hpa.toFixed(1)+' hPa';$('weather_pressure').textContent=d.weather_pressure_hpa===null?'--':d.weather_pressure_hpa.toFixed(1)+' hPa';$('barometer_pressure').textContent=d.barometer_valid?d.barometer_pressure_hpa.toFixed(1)+' hPa':'--';$('barometer_raw_pressure').textContent=d.barometer_valid?d.barometer_raw_pressure_hpa.toFixed(1)+' hPa':'--';$('barometer_temperature').textContent=d.barometer_valid?d.barometer_temperature_c.toFixed(1)+' C':'--';$('barometer_reduction_temperature').textContent=d.barometer_reduction_temperature_c===null?'--':d.barometer_reduction_temperature_c.toFixed(1)+' C';$('barometer_reduction_source').textContent=d.barometer_reduction_temperature_source;$('wu_temperature_average').textContent=d.wu_temperature_average_c===null?'bez dat':d.wu_temperature_average_c.toFixed(1)+' C | '+d.wu_temperature_sample_count+' vzorku / '+d.wu_temperature_span_h.toFixed(1)+' h';$('wu_temperature_age').textContent=d.wu_temperature_latest_epoch?age(Math.max(0,Date.now()-d.wu_temperature_latest_epoch*1000)):'bez dat';$('barometer_delta').textContent=d.barometer_delta_3h_hpa===null?'sbira se':(d.barometer_delta_3h_hpa>=0?'+':'')+d.barometer_delta_3h_hpa.toFixed(1)+' hPa';$('barometer_trend').textContent=d.barometer_trend+' | '+d.barometer_trend_hpa_h.toFixed(2)+' hPa/h';$('zambretti_code').textContent=d.zambretti_ready?d.zambretti_code:'sbira se 3h trend';$('zambretti_trend').textContent=d.zambretti_ready?d.zambretti_trend:'--';$('barometer_forecast').textContent=d.barometer_forecast;$('zambretti_wind').textContent=d.zambretti_wind_used?d.zambretti_wind_deg.toFixed(0)+' stupnu z pocasi':'bez smeru vetru';$('zambretti_season').textContent=d.zambretti_season_applied?'pouzita':'nepouzita';$('barometer_history').textContent=d.pressure_history_count+' / 289';$('barometer_age').textContent=age(d.barometer_age_ms);$('layers').textContent='Radar '+(d.radar_layer?'zapnut':'vypnut')+' | Blesky '+(d.lightning_layer?'zapnuty':'vypnuty')+' | ADS-B '+(d.adsb_layer?'zapnuto':'vypnuto');$('alerts').textContent=d.alert_enabled?d.alert_targets.filter(Boolean).join(' | '):'vypnuto';$('refresh_state').textContent='Aktualizovano '+new Date().toLocaleTimeString();}catch(e){$('refresh_state').innerHTML='<span class=bad>Diagnostiku se nepodarilo nacist: '+e+'</span>';}}loadData();setInterval(loadData,5000);</script></main></body></html>");
  return page;
}

void DeviceConfigService::handleRoot() {
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(200, "text/html; charset=utf-8", buildPage());
}

void DeviceConfigService::handleDiagnostics() {
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(200, "text/html; charset=utf-8", buildDiagnosticsPage());
}

void DeviceConfigService::handleDiagnosticsJson() {
  const uint32_t now = millis();
  const RuntimeDiagnostics emptyDiagnostics;
  const RuntimeDiagnostics& diagnostics =
      runtimeDiagnostics_ ? *runtimeDiagnostics_ : emptyDiagnostics;

  String json;
  json.reserve(4300);
  json += F("{\"firmware\":\"");
  json += FW_VERSION;
  json += F("\",\"uptime_ms\":");
  json += String(diagnostics.uptimeMs);
  json += F(",\"local_time\":\"");
  json += jsonEscape(diagnostics.localTime);
  json += F("\",\"local_date\":\"");
  json += jsonEscape(diagnostics.localDate);
  json += F("\",\"timezone\":\"");
  json += jsonEscape(diagnostics.timezone);
  json += F("\",\"time_synchronized\":");
  json += boolJson(diagnostics.timeSynchronized);
  json += F(",\"cpu_mhz\":");
  json += String(getCpuFrequencyMhz());
  json += F(",\"cpu_cores\":");
  json += String(ESP.getChipCores());
  json += F(",\"flash_bytes\":");
  json += String(ESP.getFlashChipSize());
  json += F(",\"reset_reason\":");
  json += String(static_cast<int>(esp_reset_reason()));
  json += F(",\"heap_free\":");
  json += String(ESP.getFreeHeap());
  json += F(",\"heap_min\":");
  json += String(ESP.getMinFreeHeap());
  json += F(",\"heap_largest\":");
  json += String(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  json += F(",\"psram_free\":");
  json += String(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  json += F(",\"psram_min\":");
  json += String(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
  json += F(",\"psram_largest\":");
  json += String(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  json += F(",\"wifi_connected\":");
  json += boolJson(stationConnected());
  json += F(",\"portal_active\":");
  json += boolJson(portalActive_);
  json += F(",\"network_mode\":\"");
  json += stationConnected() ? (portalActive_ ? "AP + domaci Wi-Fi" : "domaci Wi-Fi") : (portalActive_ ? "konfiguracni AP" : "offline");
  json += F("\",\"ssid\":\"");
  json += jsonEscape(stationConnected() ? WiFi.SSID().c_str() : accessPointSsid_.c_str());
  json += F("\",\"ip\":\"");
  json += stationConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  json += F("\",\"rssi_dbm\":");
  json += String(stationConnected() ? WiFi.RSSI() : 0);
  json += F(",\"hostname\":\"");
  json += Config::CONFIG_HOSTNAME;
  json += F("\",\"home_lat\":");
  json += String(settings_.homeLat, 5);
  json += F(",\"home_lon\":");
  json += String(settings_.homeLon, 5);
  json += F(",\"radar_status\":\"");
  json += jsonEscape(diagnostics.radarStatus);
  json += F("\",\"radar_frame_count\":");
  json += String(diagnostics.radarFrameCount);
  json += F(",\"current_radar_frame\":");
  json += String(diagnostics.currentRadarFrame);
  json += F(",\"radar_cache_ready\":");
  json += boolJson(diagnostics.radarCacheReady);
  json += F(",\"radar_age_ms\":");
  json += String(ageMs(now, diagnostics.lastRadarUpdateMs));
  json += F(",\"lightning_status\":\"");
  json += jsonEscape(diagnostics.lightningStatus);
  json += F("\",\"lightning_ready\":");
  json += boolJson(diagnostics.lightningReady);
  json += F(",\"lightning_strike_count\":");
  json += String(diagnostics.lightningStrikeCount);
  json += F(",\"lightning_age_ms\":");
  json += String(ageMs(now, diagnostics.lastLightningUpdateMs));
  json += F(",\"adsb_status\":\"");
  json += jsonEscape(diagnostics.adsbStatus);
  json += F("\",\"aircraft_count\":");
  json += String(static_cast<unsigned>(diagnostics.aircraftCount));
  json += F(",\"aircraft_local\":");
  json += String(static_cast<unsigned>(diagnostics.localAircraftCount));
  json += F(",\"aircraft_adsbfi\":");
  json += String(static_cast<unsigned>(diagnostics.adsbFiAircraftCount));
  json += F(",\"aircraft_mlat\":");
  json += String(static_cast<unsigned>(diagnostics.mlatAircraftCount));
  json += F(",\"adsb_age_ms\":");
  json += String(ageMs(now, diagnostics.lastAdsbUpdateMs));
  json += F(",\"weather_status\":\"");
  json += jsonEscape(diagnostics.weatherStatus);
  json += F("\",\"current_weather_valid\":");
  json += boolJson(diagnostics.currentWeatherValid);
  json += F(",\"weather_pressure_hpa\":");
  json += floatJson(diagnostics.weatherPressureHpa, 2);
  json += F(",\"weather_age_ms\":");
  json += String(ageMs(now, diagnostics.lastCurrentWeatherUpdateMs));
  json += F(",\"forecast_valid\":");
  json += boolJson(diagnostics.forecastValid);
  json += F(",\"forecast_product\":\"");
  json += jsonEscape(diagnostics.forecastProduct);
  json += F("\",\"forecast_slot_count\":");
  json += String(diagnostics.forecastSlotCount);
  json += F(",\"forecast_age_ms\":");
  json += String(ageMs(now, diagnostics.lastForecastUpdateMs));
  json += F(",\"astronomy_status\":\"");
  json += jsonEscape(diagnostics.astronomyStatus);
  json += F("\",\"astronomy_valid\":");
  json += boolJson(diagnostics.astronomyValid);
  json += F(",\"astronomy_age_ms\":");
  json += String(ageMs(now, diagnostics.lastAstronomyUpdateMs));
  json += F(",\"barometer_enabled\":");
  json += boolJson(diagnostics.barometerEnabled);
  json += F(",\"barometer_altitude_m\":");
  json += String(settings_.barometerAltitudeM, 2);
  json += F(",\"barometer_offset_hpa\":");
  json += String(settings_.barometerOffsetHpa, 2);
  json += F(",\"barometer_detected\":");
  json += boolJson(diagnostics.barometerDetected);
  json += F(",\"barometer_valid\":");
  json += boolJson(diagnostics.barometerValid);
  json += F(",\"barometer_address\":");
  json += String(diagnostics.barometerAddress);
  json += F(",\"barometer_sensor\":\"");
  json += jsonEscape(diagnostics.barometerSensor);
  json += F("\",\"barometer_status\":\"");
  json += jsonEscape(diagnostics.barometerStatus);
  json += F("\",\"barometer_pressure_hpa\":");
  json += floatJson(diagnostics.barometerPressureHpa, 2);
  json += F(",\"barometer_raw_pressure_hpa\":");
  json += floatJson(diagnostics.barometerRawPressureHpa, 2);
  json += F(",\"barometer_temperature_c\":");
  json += floatJson(diagnostics.barometerTemperatureC, 2);
  json += F(",\"barometer_reduction_temperature_c\":");
  json += floatJson(diagnostics.barometerReductionTemperatureC, 2);
  json += F(",\"barometer_reduction_temperature_source\":\"");
  json += jsonEscape(diagnostics.barometerReductionTemperatureSource);
  json += F("\",\"wu_temperature_average_c\":");
  json += floatJson(diagnostics.wuTemperatureAverageC, 2);
  json += F(",\"wu_temperature_sample_count\":");
  json += String(diagnostics.wuTemperatureSampleCount);
  json += F(",\"wu_temperature_span_h\":");
  json += floatJson(diagnostics.wuTemperatureSpanHours, 2);
  json += F(",\"wu_temperature_latest_epoch\":");
  json += String(diagnostics.wuTemperatureLatestEpoch);
  json += F(",\"barometer_delta_3h_hpa\":");
  json += floatJson(diagnostics.barometerDelta3hHpa, 2);
  json += F(",\"barometer_trend_hpa_h\":");
  json += floatJson(diagnostics.barometerTrendHpaPerHour, 3);
  json += F(",\"barometer_trend\":\"");
  json += jsonEscape(diagnostics.barometerTrend);
  json += F("\",\"barometer_forecast\":\"");
  json += jsonEscape(diagnostics.barometerForecast);
  json += F("\",\"zambretti_ready\":");
  json += boolJson(diagnostics.zambrettiReady);
  json += F(",\"zambretti_code\":\"");
  json += jsonEscape(diagnostics.zambrettiCode);
  json += F("\",\"zambretti_trend\":\"");
  json += jsonEscape(diagnostics.zambrettiTrend);
  json += F("\",\"zambretti_wind_used\":");
  json += boolJson(diagnostics.zambrettiWindUsed);
  json += F(",\"zambretti_wind_deg\":");
  json += floatJson(diagnostics.zambrettiWindDirectionDeg, 1);
  json += F(",\"zambretti_season_applied\":");
  json += boolJson(diagnostics.zambrettiSeasonApplied);
  json += F(",\"zambretti_adjusted_pressure_hpa\":");
  json += floatJson(diagnostics.zambrettiAdjustedPressureHpa, 2);
  json += F(",\"pressure_history_count\":");
  json += String(static_cast<unsigned>(diagnostics.pressureHistoryCount));
  json += F(",\"barometer_age_ms\":");
  json += String(ageMs(now, diagnostics.lastBarometerUpdateMs));
  json += F(",\"map_view\":\"");
  json += jsonEscape(diagnostics.mapView);
  json += F("\",\"map_redraw_count\":");
  json += String(diagnostics.mapRedrawCount);
  json += F(",\"last_map_redraw_ms\":");
  json += String(diagnostics.lastMapRedrawDurationMs);
  json += F(",\"lcd_resync_count\":");
  json += String(diagnostics.lcdResyncCount);
  json += F(",\"lcd_resync_age_ms\":");
  json += String(ageMs(now, diagnostics.lastDisplaySyncRecoveryMs));
  json += F(",\"backlight_on\":");
  json += boolJson(diagnostics.backlightOn);
  json += F(",\"backlight_schedule_enabled\":");
  json += boolJson(diagnostics.backlightScheduleEnabled);
  json += F(",\"backlight_window_active\":");
  json += boolJson(diagnostics.backlightScheduledWindowActive);
  json += F(",\"backlight_temporary_wake\":");
  json += boolJson(diagnostics.backlightTemporaryWake);
  json += F(",\"backlight_wake_remaining_ms\":");
  json += String(diagnostics.backlightWakeRemainingMs);
  json += F(",\"radar_layer\":");
  json += boolJson(settings_.radarLayerEnabled);
  json += F(",\"lightning_layer\":");
  json += boolJson(settings_.lightningLayerEnabled);
  json += F(",\"adsb_layer\":");
  json += boolJson(settings_.adsbLayerEnabled);
  json += F(",\"alert_enabled\":");
  json += boolJson(settings_.aircraftAlertEnabled);
  json += F(",\"alert_targets\":[");
  for (size_t slot = 0; slot < AIRCRAFT_ALERT_SLOT_COUNT; ++slot) {
    if (slot) json += ',';
    json += '"';
    json += jsonEscape(settings_.aircraftAlertTargets[slot].c_str());
    json += '"';
  }
  json += F("]}");
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(200, "application/json; charset=utf-8", json);
}

void DeviceConfigService::handleSave() {
  String newSsid = server_.arg("wifi_ssid");
  newSsid.trim();
  String newPassword = server_.arg("wifi_password");
  String newAdsbUrl = server_.arg("adsb_url");
  newAdsbUrl.trim();
  const bool newLocalAdsbEnabled = server_.hasArg("adsb_local_enabled");
  String newStation = server_.arg("wu_station");
  newStation.trim();
  String newWuKey = server_.arg("wu_key");
  newWuKey.trim();
  float newHomeLat = Config::DEFAULT_HOME_LAT;
  float newHomeLon = Config::DEFAULT_HOME_LON;
  if (!parseFloatValue(server_.arg("home_lat"), newHomeLat) ||
      !parseFloatValue(server_.arg("home_lon"), newHomeLon) ||
      newHomeLat < Config::MAP_LAT_BOTTOM || newHomeLat > Config::MAP_LAT_TOP ||
      newHomeLon < Config::MAP_LON_LEFT || newHomeLon > Config::MAP_LON_RIGHT) {
    sendErrorPage("Poloha HOME musi lezet v rozsahu mapy Ceske republiky.");
    return;
  }
  const bool newRadarLayerEnabled = server_.hasArg("layer_radar");
  const bool newLightningLayerEnabled = server_.hasArg("layer_lightning");
  const bool newAdsbLayerEnabled = server_.hasArg("layer_adsb");
  const bool newAlertEnabled = server_.hasArg("alert_enabled");
  String newAlertTargets[AIRCRAFT_ALERT_SLOT_COUNT];
  for (size_t slot = 0; slot < AIRCRAFT_ALERT_SLOT_COUNT; ++slot) {
    const String argument = String("alert_target_") + String(slot + 1);
    newAlertTargets[slot] = normalizedAircraftTarget(server_.arg(argument));
  }
  const bool newBacklightScheduleEnabled = server_.hasArg("bl_schedule");
  const bool newBarometerEnabled = server_.hasArg("baro_enabled");
  float newBarometerAltitudeM = 0.0f;
  float newBarometerOffsetHpa = 0.0f;
  if (!parseFloatValue(server_.arg("baro_altitude"), newBarometerAltitudeM) ||
      newBarometerAltitudeM < -500.0f || newBarometerAltitudeM > 5000.0f) {
    sendErrorPage("Nadmorska vyska barometru musi byt -500 az 5000 m.");
    return;
  }
  if (!parseFloatValue(server_.arg("baro_offset"), newBarometerOffsetHpa) ||
      newBarometerOffsetHpa < -50.0f || newBarometerOffsetHpa > 50.0f) {
    sendErrorPage("Jemna korekce MSL tlaku musi byt -50 az 50 hPa.");
    return;
  }
  BacklightDaySchedule newBacklightDays[BACKLIGHT_DAY_COUNT];
  for (size_t day = 0; day < BACKLIGHT_DAY_COUNT; ++day) {
    newBacklightDays[day].enabled =
        server_.hasArg(String("bl_day_") + String(day));
    const String startArgument = String("bl_start_") + String(day);
    const String endArgument = String("bl_end_") + String(day);
    if (!parseTimeMinutes(server_.arg(startArgument),
                          newBacklightDays[day].startMinutes) ||
        !parseTimeMinutes(server_.arg(endArgument),
                          newBacklightDays[day].endMinutes)) {
      sendErrorPage(String("Neplatny cas podsviceni pro den ") +
                    kBacklightDayNames[day] + ".");
      return;
    }
  }

  if (newSsid.isEmpty()) {
    sendErrorPage("SSID nesmi byt prazdne.");
    return;
  }
  if (!newAdsbUrl.isEmpty() && !urlLooksValid(newAdsbUrl)) {
    sendErrorPage("ADSB URL musi zacinat http:// nebo https://, nebo muze byt prazdna.");
    return;
  }
  if (newLocalAdsbEnabled && newAdsbUrl.isEmpty()) {
    sendErrorPage("Pri zapnutem lokalnim ADS-B zadejte URL aircraft.json.");
    return;
  }
  bool anyAlertTarget = false;
  for (size_t slot = 0; slot < AIRCRAFT_ALERT_SLOT_COUNT; ++slot) {
    anyAlertTarget |= !newAlertTargets[slot].isEmpty();
  }
  if (newAlertEnabled && !anyAlertTarget) {
    sendErrorPage("Pri zapnutem zvyrazneni zadejte alespon jeden callsign nebo ICAO hex.");
    return;
  }

  const bool ssidChanged = newSsid != settings_.wifiSsid;
  const bool passwordChanged = !newPassword.isEmpty();
  const bool networkChanged = ssidChanged || passwordChanged;

  settings_.wifiSsid = newSsid;
  if (passwordChanged || ssidChanged) settings_.wifiPassword = newPassword;
  settings_.adsbUrl = newAdsbUrl;
  settings_.localAdsbEnabled = newLocalAdsbEnabled;
  settings_.homeLat = newHomeLat;
  settings_.homeLon = newHomeLon;
  settings_.wuStationId = newStation;
  if (!newWuKey.isEmpty()) settings_.wuApiKey = newWuKey;
  settings_.radarLayerEnabled = newRadarLayerEnabled;
  settings_.lightningLayerEnabled = newLightningLayerEnabled;
  settings_.adsbLayerEnabled = newAdsbLayerEnabled;
  settings_.aircraftAlertEnabled = newAlertEnabled;
  for (size_t slot = 0; slot < AIRCRAFT_ALERT_SLOT_COUNT; ++slot) {
    settings_.aircraftAlertTargets[slot] = newAlertTargets[slot];
  }
  settings_.backlightScheduleEnabled = newBacklightScheduleEnabled;
  settings_.barometerEnabled = newBarometerEnabled;
  settings_.barometerAltitudeM = newBarometerAltitudeM;
  settings_.barometerOffsetHpa = newBarometerOffsetHpa;
  for (size_t day = 0; day < BACKLIGHT_DAY_COUNT; ++day) {
    settings_.backlightDays[day] = newBacklightDays[day];
  }

  if (!saveSettings()) {
    sendErrorPage("Nastaveni se nepodarilo ulozit do NVS.", 500);
    return;
  }

  runtimeSettingsChanged_ = true;
  DebugLog::printf(
      "Config: saved, SSID=%s, home=%.5f,%.5f, localADSB=%s, layers=%s, alerts=%s [%s|%s|%s], barometer=%s alt=%.1f offset=%+.1f\n",
      settings_.wifiSsid.c_str(), settings_.homeLat, settings_.homeLon,
      settings_.localAdsbEnabled ? "on" : "off",
      layerSummary(settings_.radarLayerEnabled, settings_.lightningLayerEnabled,
                   settings_.adsbLayerEnabled).c_str(),
      settings_.aircraftAlertEnabled ? "on" : "off",
      settings_.aircraftAlertTargets[0].c_str(),
      settings_.aircraftAlertTargets[1].c_str(),
      settings_.aircraftAlertTargets[2].c_str(),
      settings_.barometerEnabled ? "on" : "off",
      settings_.barometerAltitudeM, settings_.barometerOffsetHpa);

  if (networkChanged) {
    server_.send(200, "text/html; charset=utf-8",
                 "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'><style>body{font-family:Arial;background:#07131c;color:white;padding:30px}</style><h1>Ulozeno</h1><p>Wi-Fi byla zmenena. Zarizeni se restartuje.</p>");
    restartPending_ = true;
    restartAt_ = millis() + 1200;
  } else {
    server_.sendHeader("Location", "/", true);
    server_.send(303, "text/plain", "Nastaveni bylo pouzito.");
  }
}

void DeviceConfigService::handleFactoryReset() {
  Preferences preferences;
  if (preferences.begin(kPreferencesNamespace, false)) {
    preferences.clear();
    preferences.end();
  }
  if (preferences.begin("mapview", false)) {
    preferences.clear();
    preferences.end();
  }
  server_.send(200, "text/html; charset=utf-8",
               "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'><style>body{font-family:Arial;background:#07131c;color:white;padding:30px}</style><h1>Nastaveni smazano</h1><p>Po restartu se spusti prvotni konfiguracni AP.</p>");
  restartPending_ = true;
  restartAt_ = millis() + 1200;
}

void DeviceConfigService::handleReboot() {
  server_.send(200, "text/html; charset=utf-8",
               "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'><style>body{font-family:Arial;background:#07131c;color:white;padding:30px}</style><h1>Restart</h1><p>Zarizeni se restartuje.</p>");
  restartPending_ = true;
  restartAt_ = millis() + 800;
}

void DeviceConfigService::handleLcdResync() {
  lcdResyncRequested_ = true;
  DebugLog::println("Config: manual LCD resync requested");
  server_.sendHeader("Location", "/", true);
  server_.send(303, "text/plain", "LCD resync scheduled");
}

void DeviceConfigService::handleOtaPrepare() {
  otaSucceeded_ = false;
  otaError_ = 0;
  otaBytesWritten_ = 0;
  otaExpectedBytes_ = 0;
  otaFilename_ = server_.hasArg("name") ? server_.arg("name") : String("firmware.bin");
  if (server_.hasArg("size")) {
    const long parsedSize = server_.arg("size").toInt();
    if (parsedSize > 0) otaExpectedBytes_ = static_cast<uint32_t>(parsedSize);
  }

  DebugLog::printf("OTA: preflight %s, expected=%u B\n",
                   otaFilename_.c_str(),
                   static_cast<unsigned>(otaExpectedBytes_));

  server_.sendHeader("Cache-Control", "no-store");
  server_.sendHeader("Connection", "close");
  server_.send(204, "text/plain", "");

  // Defer all display work until after WebServer has completed this request.
  // The browser waits briefly before starting the multipart firmware upload.
  otaPrepareDisplayPending_ = true;
}

void DeviceConfigService::handleOtaUpload() {
  HTTPUpload& upload = server_.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaInProgress_ = true;
    otaSucceeded_ = false;
    otaBytesWritten_ = 0;
    otaError_ = 0;
    otaFilename_ = upload.filename;
    otaExpectedBytes_ = 0;
    if (server_.hasArg("size")) {
      const long parsedSize = server_.arg("size").toInt();
      if (parsedSize > 0) otaExpectedBytes_ = static_cast<uint32_t>(parsedSize);
    }
    DebugLog::printf("OTA: start %s, expected=%u B, free sketch=%u B\n",
                     upload.filename.c_str(),
                     static_cast<unsigned>(otaExpectedBytes_),
                     static_cast<unsigned>(ESP.getFreeSketchSpace()));

    if (!upload.filename.endsWith(".bin")) {
      otaError_ = -10;
      DebugLog::println("OTA: rejected non-.bin file");
      otaDisplayFailurePending_ = true;
      return;
    }

    const size_t updateSize = otaExpectedBytes_ > 0
                                  ? static_cast<size_t>(otaExpectedBytes_)
                                  : UPDATE_SIZE_UNKNOWN;
    DebugLog::printf("OTA: calling Update.begin(%u)\n",
                     static_cast<unsigned>(updateSize));
    if (!Update.begin(updateSize, U_FLASH)) {
      otaError_ = static_cast<int>(Update.getError());
      DebugLog::printf("OTA: Update.begin failed error=%d\n", otaError_);
      Update.printError(Serial);
      otaDisplayFailurePending_ = true;
    } else {
      DebugLog::println("OTA: Update.begin OK, waiting for firmware chunks");
      // Progress(0) performs no LVGL or RGB-panel DMA work. It only cancels
      // the preflight timeout and blanks the physical backlight before writes.
      if (otaDisplayCallback_) {
        otaDisplayCallback_(OtaDisplayEvent::Progress, otaFilename_.c_str(), 0, 0);
      }
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (otaError_ != 0 || !otaInProgress_) return;
    if (otaBytesWritten_ == 0) {
      DebugLog::printf("OTA: first chunk %u B, uploadTotal=%u B\n",
                       static_cast<unsigned>(upload.currentSize),
                       static_cast<unsigned>(upload.totalSize));
    }
    const uint32_t beforeWrite = otaBytesWritten_;
    const size_t written = Update.write(upload.buf, upload.currentSize);
    otaBytesWritten_ += static_cast<uint32_t>(written);
    if (written != upload.currentSize) {
      otaError_ = static_cast<int>(Update.getError());
      if (otaError_ == 0) otaError_ = -11;
      DebugLog::printf("OTA: write failed %u/%u error=%d\n",
                       static_cast<unsigned>(written),
                       static_cast<unsigned>(upload.currentSize), otaError_);
      Update.printError(Serial);
      // Do not touch the display from the synchronous WRITE callback, even
      // on failure. The POST result handler will present the error after the
      // multipart request has finished.
      return;
    }

    if ((beforeWrite / (256U * 1024U)) !=
        (otaBytesWritten_ / (256U * 1024U))) {
      DebugLog::printf("OTA: written %u / %u B\n",
                       static_cast<unsigned>(otaBytesWritten_),
                       static_cast<unsigned>(otaExpectedBytes_));
    }

    // IMPORTANT: do not touch LVGL or restart the RGB panel while flash is
    // being erased/written. WebServer upload callbacks run synchronously.
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (otaError_ == 0 && otaExpectedBytes_ > 0 &&
        otaBytesWritten_ != otaExpectedBytes_) {
      otaError_ = -13;
      DebugLog::printf("OTA: size mismatch expected=%u written=%u uploadTotal=%u\n",
                       static_cast<unsigned>(otaExpectedBytes_),
                       static_cast<unsigned>(otaBytesWritten_),
                       static_cast<unsigned>(upload.totalSize));
      Update.abort();
    }

    if (otaError_ == 0) {
      if (Update.end(true)) {
        otaSucceeded_ = true;
        DebugLog::printf("OTA: complete, %u bytes written (expected %u)\n",
                         static_cast<unsigned>(otaBytesWritten_),
                         static_cast<unsigned>(otaExpectedBytes_));

        // Arm reboot here, before the normal POST result handler. If the
        // browser closes the connection after the final multipart chunk, the
        // new firmware is still guaranteed to boot once handleClient returns.
        restartPending_ = true;
        restartAt_ = millis() + 2200U;
        DebugLog::println("OTA: reboot armed after successful Update.end");
      } else {
        otaError_ = static_cast<int>(Update.getError());
        DebugLog::printf("OTA: finalize failed error=%d\n", otaError_);
        Update.printError(Serial);
        otaDisplayFailurePending_ = true;
      }
    } else {
      Update.abort();
    }

    // Keep otaInProgress_ asserted until the normal POST result handler has
    // sent the browser response. The main loop therefore cannot resume map
    // rendering in the short gap between the last upload chunk and result.
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaError_ = -12;
    otaSucceeded_ = false;
    otaInProgress_ = false;
    DebugLog::println("OTA: upload aborted");
    otaDisplayFailurePending_ = true;
  }
}

void DeviceConfigService::handleOtaResult() {
  otaInProgress_ = false;
  if (otaSucceeded_ && otaError_ == 0) {
    // Update.end() has already armed a fallback reboot. Once the HTTP result
    // handler is reached, shorten the delay so the browser has time to receive
    // the success page but the device cannot remain on the old image.
    restartPending_ = true;
    restartAt_ = millis() + 1000U;
    DebugLog::println("OTA: success response, reboot in 1 s");

    String page;
    page.reserve(900);
    page += F("<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'><style>body{font-family:Arial;background:#07131c;color:white;padding:30px;max-width:700px;margin:auto}.ok{color:#6ee7a5}</style><h1 class='ok'>OTA aktualizace uspesna</h1><p>Nahrano ");
    page += String(otaBytesWritten_);
    page += F(" B. Zarizeni se restartuje do noveho firmware.</p><p>Po nekolika sekundach obnovte <code>http://radar-adsb.local/</code>.</p>");
    server_.sendHeader("Cache-Control", "no-store");
    server_.sendHeader("Connection", "close");
    server_.send(200, "text/html; charset=utf-8", page);
    return;
  }

  String message = F("OTA aktualizace selhala. Kod chyby: ");
  message += String(otaError_);
  message += F(". Puvodni firmware zustava aktivni.");
  DebugLog::printf("OTA: failed error=%d bytes=%u expected=%u\n", otaError_,
                   static_cast<unsigned>(otaBytesWritten_),
                   static_cast<unsigned>(otaExpectedBytes_));
  otaDisplayFailurePending_ = true;
  // Flash writes can temporarily disturb the RGB display DMA on ESP32-S3.
  // A failed OTA does not reboot, so force a clean LCD redraw afterwards.
  lcdResyncRequested_ = true;
  sendErrorPage(message, 500);
}

void DeviceConfigService::handleStatusJson() {
  String json;
  json.reserve(768);
  json += F("{\"firmware\":\"");
  json += FW_VERSION;
  json += F("\",\"wifi_connected\":");
  json += boolJson(stationConnected());
  json += F(",\"portal_active\":");
  json += boolJson(portalActive_);
  json += F(",\"ip\":\"");
  json += stationConnected() ? WiFi.localIP().toString()
                             : WiFi.softAPIP().toString();
  json += F("\",\"radar_layer\":");
  json += boolJson(settings_.radarLayerEnabled);
  json += F(",\"lightning_layer\":");
  json += boolJson(settings_.lightningLayerEnabled);
  json += F(",\"adsb_layer\":");
  json += boolJson(settings_.adsbLayerEnabled);
  json += F(",\"alert_enabled\":");
  json += boolJson(settings_.aircraftAlertEnabled);
  json += F(",\"alert_targets\":[");
  for (size_t slot = 0; slot < AIRCRAFT_ALERT_SLOT_COUNT; ++slot) {
    if (slot) json += ',';
    json += F("{\"id\":\"");
    json += settings_.aircraftAlertTargets[slot];
    json += F("\",\"present\":");
    json += boolJson(alertTargetPresent(slot));
    json += '}';
  }
  json += F("]}");
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(200, "application/json", json);
}

void DeviceConfigService::handleCaptivePortal() {
  if (portalActive_) {
    server_.sendHeader("Location", "http://192.168.4.1/", true);
    server_.send(302, "text/plain", "");
  } else {
    handleRoot();
  }
}

void DeviceConfigService::sendErrorPage(const String& message, int code) {
  String page = "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'><style>body{font-family:Arial;background:#07131c;color:white;padding:30px}a{color:#8bd5ff}</style><h1>Chyba nastaveni</h1><p>";
  page += htmlEscape(message);
  page += "</p><p><a href='/'>Zpet do nastaveni</a></p>";
  server_.send(code, "text/html; charset=utf-8", page);
}
