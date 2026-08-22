from pathlib import Path

root = Path(__file__).resolve().parents[1]
h = (root / 'src/device_config.h').read_text()
cpp = (root / 'src/device_config.cpp').read_text()
version = (root / 'include/version.h').read_text()

assert '0.30.3-home-map-buttons' in version
assert 'WIFI_PROFILE_COUNT = 5' in h
assert 'WifiProfile wifiProfiles[WIFI_PROFILE_COUNT]' in h
assert 'wifi_multi' in cpp
for key in ('wifi_en%u', 'wifi_ssid%u', 'wifi_pass%u'):
    assert key in cpp
assert 'migrated legacy Wi-Fi to profile 1' in cpp
assert 'connectStation(23000)' in cpp
assert 'trying profile %u/%u' in cpp
assert 'nextWifiProfileIndex_' in h and 'activeWifiProfile_' in h
assert 'WL_NO_SSID_AVAIL' in cpp and 'WL_CONNECT_FAILED' in cpp
assert "wifi_enabled_" in cpp
assert 'Wi-Fi profily' in cpp
assert 'Wi-Fi profile NVS read-back verification failed' in cpp
assert 'all saved Wi-Fi profiles failed' in cpp
assert 'no Wi-Fi profile is enabled' in cpp
assert 'WiFi.mode(hasEnabledWifiProfile() ? WIFI_AP_STA : WIFI_AP)' in cpp
print('WIFI PROFILES TEST OK')
