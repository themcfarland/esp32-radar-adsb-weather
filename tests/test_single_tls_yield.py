from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
version = (ROOT / "include/version.h").read_text()
config = (ROOT / "include/config.h").read_text()
main = (ROOT / "src/main.cpp").read_text()
worker = (ROOT / "src/network_worker.cpp").read_text()
light_h = (ROOT / "src/lightning_service.h").read_text()
light = (ROOT / "src/lightning_service.cpp").read_text()
models = (ROOT / "include/models.h").read_text()
device = (ROOT / "src/device_config.cpp").read_text()

assert "0.30.16-adsbfi-110nm" in version
assert "ADSB_FI_REFRESH_MS = 30UL * 1000UL" in config
assert "SINGLE_TLS_LIGHTNING_YIELD_TIMEOUT_MS = 5000UL" in config
assert "SINGLE_TLS_POST_YIELD_SETTLE_MS = 20UL" in config
assert "pauseForExternalTls" in light_h and "resumeAfterExternalTls" in light_h
assert "externalTlsPauseRequested_" in light_h and "externalTlsPaused_" in light_h
assert "externalTlsPauseCount_" in light_h
assert "WSS yielded for external TLS" in light
assert "ExternalTlsScope" in worker
assert "job != Job::AdsbLocal" in worker
assert "pdMS_TO_TICKS(Config::SINGLE_TLS_POST_YIELD_SETTLE_MS)" in worker
assert "networkWorker.begin(&radar, &lightning)" in main
assert "lightningTlsPaused" in models and "lightningTlsPauseCount" in models
assert "lightning_tls_pause_count" in device
priority = worker.split("static constexpr Job priority[]",1)[1].split("};",1)[0]
assert priority.find("Job::Radar") < priority.find("Job::AdsbInternet")
assert priority.find("Job::WeatherCurrent") < priority.find("Job::AdsbInternet")
print("SINGLE TLS YIELD TEST OK")
