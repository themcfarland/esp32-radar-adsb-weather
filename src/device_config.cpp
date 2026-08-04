#include "device_config.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ctype.h>

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

const char* layerSummary(bool radar, bool adsb) {
  if (radar && adsb) return "radar + ADS-B";
  if (radar) return "jen radar";
  if (adsb) return "jen ADS-B";
  return "bez radaru a ADS-B";
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
      settings_.wuStationId = preferences.getString("wu_station", "IPLZE179");
      settings_.adsbUrl = preferences.getString("adsb_url", "");
      settings_.radarLayerEnabled = preferences.getBool("layer_radar", true);
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

      DebugLog::printf("Config: restored for SSID '%s', layers=%s\n",
                       settings_.wifiSsid.c_str(),
                       layerSummary(settings_.radarLayerEnabled,
                                    settings_.adsbLayerEnabled));
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
  }
  if (settings_.wuStationId.isEmpty()) settings_.wuStationId = "IPLZE179";
}

void DeviceConfigService::begin(const AircraftSnapshot* aircraftSnapshot) {
  aircraftSnapshot_ = aircraftSnapshot;
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
  server_.on("/api/status", HTTP_GET, [this]() { handleStatusJson(); });

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
  ok &= preferences.putString("adsb_url", settings_.adsbUrl) > 0;
  preferences.putBool("layer_radar", settings_.radarLayerEnabled);
  preferences.putBool("layer_adsb", settings_.adsbLayerEnabled);
  preferences.putBool("alert_on", settings_.aircraftAlertEnabled);
  preferences.putString("alert_id0", settings_.aircraftAlertTargets[0]);
  preferences.putString("alert_id1", settings_.aircraftAlertTargets[1]);
  preferences.putString("alert_id2", settings_.aircraftAlertTargets[2]);
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
  page.reserve(26000);
  page += F("<!doctype html><html lang='cs'><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>Radar ADS-B nastaveni</title><style>");
  page += F("body{font-family:Arial,sans-serif;background:#07131c;color:#e8f1f5;margin:0;padding:18px}");
  page += F("main{max-width:820px;margin:auto}.card{background:#102433;border:1px solid #29495c;border-radius:12px;padding:16px;margin-bottom:14px}");
  page += F("h1{font-size:24px;margin:0 0 8px}h2{font-size:18px;margin:0 0 12px;color:#8bd5ff}");
  page += F("label{display:block;margin:12px 0 5px}input,select{width:100%;box-sizing:border-box;padding:11px;border-radius:8px;border:1px solid #456275;background:#071923;color:#fff}");
  page += F("input[type=checkbox]{width:auto;margin-right:8px}.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}.three{display:grid;grid-template-columns:1fr 1fr 1fr;gap:12px}");
  page += F("button{background:#1976a8;color:white;border:0;border-radius:8px;padding:11px 16px;font-size:15px;cursor:pointer}.small{padding:8px 10px;margin:8px 5px 0 0}.danger{background:#a83a3a}.muted{color:#a7bac5;font-size:13px}.ok{color:#6ee7a5}.warn{color:#ffd166}");
  page += F("code{background:#071923;padding:2px 5px;border-radius:4px}@media(max-width:700px){.row,.three{grid-template-columns:1fr}}</style></head><body><main>");
  page += F("<h1>Radar CR + ADS-B</h1><p class='muted'>Firmware ");
  page += htmlEscape(FW_VERSION);
  page += F("</p><section class='card'><h2>Stav a pristup</h2><p>Sit: <strong>");
  page += htmlEscape(networkLabel());
  page += F("</strong></p><p>Konfigurace v domaci siti: <code>");
  page += htmlEscape(accessUrl());
  page += F("</code> nebo <code>http://");
  page += htmlEscape(Config::CONFIG_HOSTNAME);
  page += F(".local/</code></p><p>Aktivni vrstvy: <strong>");
  page += layerSummary(settings_.radarLayerEnabled, settings_.adsbLayerEnabled);
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
  page += F(">Zobrazit radarovou vrstvu</label><label><input type='checkbox' name='layer_adsb' value='1'");
  if (settings_.adsbLayerEnabled) page += F(" checked");
  page += F(">Zobrazit letouny ADS-B</label><p class='muted'>Lze zvolit radar + ADS-B, jen radar, jen ADS-B nebo obe vrstvy vypnout. Zmena se po ulozeni projevi bez restartu.</p></section>");

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

  page += F("<section class='card'><h2>Datove zdroje</h2><label for='adsb_url'>URL aircraft.json</label><input id='adsb_url' name='adsb_url' maxlength='180' required value='");
  page += htmlEscape(settings_.adsbUrl);
  page += F("'><div class='row'><div><label for='wu_station'>WU stanice</label><input id='wu_station' name='wu_station' maxlength='20' value='");
  page += htmlEscape(settings_.wuStationId);
  page += F("'></div><div><label for='wu_key'>Novy WU API klic</label><input id='wu_key' name='wu_key' type='password' maxlength='96' placeholder='Prazdne = zachovat ulozeny klic'></div></div><p class='muted'>Predpoved Open-Meteo funguje i bez WU klice.</p></section>");

  page += F("<button type='submit'>Ulozit nastaveni</button></form><section class='card' style='margin-top:14px'><h2>Servis</h2><form method='post' action='/reboot' style='display:inline'><button type='submit'>Restart</button></form> <form method='post' action='/factory-reset' style='display:inline'><button type='submit' class='danger'>Smazat nastaveni</button></form></section>");
  page += F("<script>const s=document.getElementById('aircraft_now');document.querySelectorAll('.assign').forEach(b=>b.addEventListener('click',()=>{if(s.value)document.getElementById('alert_target_'+b.dataset.slot).value=s.value;}));</script></main></body></html>");
  return page;
}

void DeviceConfigService::handleRoot() {
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(200, "text/html; charset=utf-8", buildPage());
}

void DeviceConfigService::handleSave() {
  String newSsid = server_.arg("wifi_ssid");
  newSsid.trim();
  String newPassword = server_.arg("wifi_password");
  String newAdsbUrl = server_.arg("adsb_url");
  newAdsbUrl.trim();
  String newStation = server_.arg("wu_station");
  newStation.trim();
  String newWuKey = server_.arg("wu_key");
  newWuKey.trim();
  const bool newRadarLayerEnabled = server_.hasArg("layer_radar");
  const bool newAdsbLayerEnabled = server_.hasArg("layer_adsb");
  const bool newAlertEnabled = server_.hasArg("alert_enabled");
  String newAlertTargets[AIRCRAFT_ALERT_SLOT_COUNT];
  for (size_t slot = 0; slot < AIRCRAFT_ALERT_SLOT_COUNT; ++slot) {
    const String argument = String("alert_target_") + String(slot + 1);
    newAlertTargets[slot] = normalizedAircraftTarget(server_.arg(argument));
  }

  if (newSsid.isEmpty()) {
    sendErrorPage("SSID nesmi byt prazdne.");
    return;
  }
  if (!urlLooksValid(newAdsbUrl)) {
    sendErrorPage("ADSB URL musi zacinat http:// nebo https://.");
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
  settings_.wuStationId = newStation;
  if (!newWuKey.isEmpty()) settings_.wuApiKey = newWuKey;
  settings_.radarLayerEnabled = newRadarLayerEnabled;
  settings_.adsbLayerEnabled = newAdsbLayerEnabled;
  settings_.aircraftAlertEnabled = newAlertEnabled;
  for (size_t slot = 0; slot < AIRCRAFT_ALERT_SLOT_COUNT; ++slot) {
    settings_.aircraftAlertTargets[slot] = newAlertTargets[slot];
  }

  if (!saveSettings()) {
    sendErrorPage("Nastaveni se nepodarilo ulozit do NVS.", 500);
    return;
  }

  runtimeSettingsChanged_ = true;
  DebugLog::printf(
      "Config: saved, SSID=%s, layers=%s, alerts=%s [%s|%s|%s]\n",
      settings_.wifiSsid.c_str(),
      layerSummary(settings_.radarLayerEnabled, settings_.adsbLayerEnabled),
      settings_.aircraftAlertEnabled ? "on" : "off",
      settings_.aircraftAlertTargets[0].c_str(),
      settings_.aircraftAlertTargets[1].c_str(),
      settings_.aircraftAlertTargets[2].c_str());

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
