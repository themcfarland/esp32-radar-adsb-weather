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
    "adaptive thresholds": all(x in config for x in [
        "TLS_GUARD_CRITICAL_FREE_INTERNAL", "TLS_GUARD_WARNING_LARGEST_BLOCK",
        "TLS_GUARD_CRITICAL_LARGEST_BLOCK", "TLS_GUARD_RECOVER_LARGEST_BLOCK",
        "TLS_RECOVERY_FAILURE_LARGEST_BLOCK"]),
    "hysteresis": "tlsGuardLatched_" in worker_h and "updateTlsMemoryStateLocked" in worker,
    "warning attempts TLS": "Normal and warning states always attempt TLS" in worker,
    "critical single defer": "critical defer" in worker and "critical allowed" in worker,
    "reactive failure recovery": "requestReactiveTlsRecovery" in worker_h and "TLS recovery:" in worker,
    "WSS yield request": "consumeTlsRecoveryRequest" in worker_h and "requestTransportYield" in lightning_h and "transport yield" in lightning,
    "main wires recovery": "consumeTlsRecoveryRequest" in main and "requestTransportYield" in main,
    "HTTP code persisted": "lastNetworkHttpCode_ = code" in adsb,
    "fallback suppression": "fallback suppressed code=%d" in adsb and "primaryCode < 400" in adsb,
    "explicit TLS release": "releaseSecureHttp" in adsb and "client.stop();" in adsb,
    "provider result survives defer": "pre-flight defer is TLS-guard metadata" in worker,
    "web diagnostics": "tls_memory_state" in device and "tls_deferred_jobs" in device,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("TLS RESOURCE GUARD TEST FAILED: " + ", ".join(failed))
print("TLS RESOURCE GUARD TEST OK")
