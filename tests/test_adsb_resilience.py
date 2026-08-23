from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
config = (ROOT / "include" / "config.h").read_text(encoding="utf-8")
worker = (ROOT / "src" / "network_worker.cpp").read_text(encoding="utf-8")
radar = (ROOT / "src" / "radar_service.cpp").read_text(encoding="utf-8")
adsb = (ROOT / "src" / "adsb_service.cpp").read_text(encoding="utf-8")
version = (ROOT / "include" / "version.h").read_text(encoding="utf-8")

assert '0.30.5-network-recovery' in version
assert 'ADSB_LOCAL_CACHE_MAX_AGE_MS = 60UL * 1000UL' in config
assert 'ADSB_FI_CACHE_MAX_AGE_MS = 120UL * 1000UL' in config
assert 'Job::AdsbLocal, Job::AdsbInternet, Job::WeatherCurrent' in worker
assert 'kIndexTotalTimeoutMs = 15000UL' in radar
assert 'kIndexNoDataTimeoutMs = 5000UL' in radar
assert 'slow failure' in adsb and 'fallback deferred' in adsb
print('ADSB RESILIENCE TEST OK')
