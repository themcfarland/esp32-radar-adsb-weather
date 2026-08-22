from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "src/main.cpp").read_text(encoding="utf-8")
config = (root / "include/config.h").read_text(encoding="utf-8")
web = (root / "src/device_config.cpp").read_text(encoding="utf-8")

assert "DISPLAY_LOAD_GUARD_THRESHOLD_MS = 1500UL" in config
assert "DISPLAY_LOAD_GUARD_COOLDOWN_MS = 90UL * 1000UL" in config
assert "scheduleDisplayRecoveryAfterLoad" in main
assert "loopDurationMs = millis() - loopStartedMs" in main
assert "displayResyncPending = true" in main
assert "periodicky restart RGB DMA" not in web or "nejde o periodicky restart displeje" in web
assert "DISPLAY_SYNC_RECOVERY_MS" not in config
print("DISPLAY LOAD GUARD TEST OK")
