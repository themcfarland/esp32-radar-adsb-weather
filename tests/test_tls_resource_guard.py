#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
config = (ROOT / "include/config.h").read_text(encoding="utf-8")
worker_h = (ROOT / "src/network_worker.h").read_text(encoding="utf-8")
worker = (ROOT / "src/network_worker.cpp").read_text(encoding="utf-8")
lightning_h = (ROOT / "src/lightning_service.h").read_text(encoding="utf-8")
lightning = (ROOT / "src/lightning_service.cpp").read_text(encoding="utf-8")
adsb = (ROOT / "src/adsb_service.cpp").read_text(encoding="utf-8")
device = (ROOT / "src/device_config.cpp").read_text(encoding="utf-8")
main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")

checks = {
    "TLS thresholds": "TLS_GUARD_MIN_FREE_INTERNAL" in config and "TLS_GUARD_MIN_LARGEST_BLOCK" in config,
    "worker preflight": "tlsPreflight" in worker_h and "TLS guard: defer" in worker,
    "bounded forced attempt": "TLS_GUARD_FORCE_AFTER_DEFERS" in config and "tlsForcedAttempts" in worker,
    "WSS yield request": "consumeTlsRecoveryRequest" in worker_h and "requestTransportYield" in lightning_h and "transport yield" in lightning,
    "main wires recovery": "consumeTlsRecoveryRequest" in main and "requestTransportYield" in main,
    "HTTP code persisted": "lastNetworkHttpCode_ = code" in adsb,
    "fallback suppression": "fallback suppressed code=%d" in adsb and "primaryCode < 400" in adsb,
    "explicit TLS release": "releaseSecureHttp" in adsb and "client.stop();" in adsb,
    "web diagnostics": "tls_guard_low_memory" in device and "tls_deferred_jobs" in device,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("TLS RESOURCE GUARD TEST FAILED: " + ", ".join(failed))
print("TLS RESOURCE GUARD TEST OK")
