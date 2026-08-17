#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
config = (ROOT / "include/config.h").read_text()
hdr = (ROOT / "src/lightning_service.h").read_text()
cpp = (ROOT / "src/lightning_service.cpp").read_text()

assert "LIGHTNING_FIRST_DATA_TIMEOUT_MS = 30UL * 1000UL" in config
assert "LIGHTNING_STALE_DATA_TIMEOUT_MS = 30UL * 1000UL" in config
assert "void forceReconnect(const char* reason);" in hdr
assert "lastValidFrameMs_ = lastSuccessMs_;" in cpp
assert "forceReconnect(\"no first valid frame\")" in cpp
assert "forceReconnect(\"no valid data for 30 s\")" in cpp
assert "Older trail entries are deliberately point-like" in cpp
assert "drawStrike(destination, width, height, x, y, color, ageSec);" in cpp
print("LIGHTNING STREAM GUARD TEST OK")
