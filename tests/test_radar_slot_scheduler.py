from pathlib import Path

root = Path(__file__).resolve().parents[1]
config = (root / "include/config.h").read_text()
header = (root / "src/radar_service.h").read_text()
radar = (root / "src/radar_service.cpp").read_text()
main = (root / "src/main.cpp").read_text()
version = (root / "include/version.h").read_text()
device = (root / "src/device_config.cpp").read_text()

assert "0.30.16-adsbfi-110nm" in version
assert "RADAR_PUBLICATION_DELAY_SEC = 30UL" in config
assert "RADAR_RETRY_STEP_SEC = 20UL" in config
assert "RADAR_SLOT_ATTEMPTS = 3" in config
assert "RADAR_INDEX_FALLBACK_MISSED_SLOTS = 3" in config
assert "runtimeRefreshDue(time_t nowUtc)" in header
assert "secondsUntilNextRuntimeRefresh(time_t nowUtc)" in header
assert "expectedRadarSlotUtc" in radar
assert "Radar: cekam na novy 5min snimek" in radar
assert "staleEnough && fallbackCooldownOk" in radar
assert "lastIndexFallbackUtc_" in header
assert "radar.runtimeRefreshDue(wallNow)" in main
assert "RADAR_REFRESH_MS" in main  # only NTP fallback remains
assert 'radar_expected_frame' in device
assert 'radar_direct_404' in device
print("RADAR SLOT SCHEDULER TEST OK")

assert 'pacz2gmaps3.z_max3d.' in radar, 'MAX_Z masked filename prefix changed unexpectedly'
assert 'markRuntimeRefreshQueued' in header and 'markRuntimeRefreshQueued' in radar, 'queued-attempt commit missing'
assert 'request(NetworkWorker::Job::Radar, true)' in main, 'scheduled radar retries must bypass previous radar backoff'
assert 'if (token == lastScheduledAttemptToken_) return false;\n  lastScheduledAttemptToken_ = token;' not in radar, 'runtimeRefreshDue must not consume an unqueued attempt'
assert 'radar_direct_transport_failures' in device, 'radar transport diagnostics missing'
assert 'radar_last_http_code' in device, 'radar last HTTP code diagnostic missing'
