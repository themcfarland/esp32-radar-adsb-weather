#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
config = (ROOT / "include/config.h").read_text()
hdr = (ROOT / "src/lightning_service.h").read_text()
cpp = (ROOT / "src/lightning_service.cpp").read_text()

assert "LIGHTNING_FIRST_DATA_TIMEOUT_MS = 60UL * 1000UL" in config
assert "LIGHTNING_STALE_DATA_TIMEOUT_MS = 120UL * 1000UL" in config
assert "void forceReconnect(const char* reason);" in hdr
assert "lastValidFrameMs_ = lastSuccessMs_;" in cpp
assert 'forceReconnect("no first JSON frame")' in cpp
assert 'forceReconnect("no valid JSON data")' in cpp
assert "valid envelope counts even when strokes[] is empty" in cpp
assert "Older trail entries are deliberately point-like" in cpp
print("LIGHTNING JSON STREAM GUARD TEST OK")
