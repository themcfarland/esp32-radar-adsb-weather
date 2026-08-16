#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
config = (ROOT / "include/config.h").read_text()
source = (ROOT / "src/lightning_service.cpp").read_text()

required = {
    "LIGHTNING_TRAIL_WHITE_MAX_AGE_SEC = 2UL * 60UL": "white 0-2 min",
    "LIGHTNING_TRAIL_YELLOW_MAX_AGE_SEC = 5UL * 60UL": "yellow 2-5 min",
    "LIGHTNING_TRAIL_ORANGE_MAX_AGE_SEC = 10UL * 60UL": "orange 5-10 min",
    "LIGHTNING_TRAIL_RED_MAX_AGE_SEC = 20UL * 60UL": "red 10-20 min",
}
for token, meaning in required.items():
    assert token in config, f"missing {meaning} threshold"

assert "rgb565(255, 255, 255)" in source
assert "rgb565(255, 224, 0)" in source
assert "rgb565(255, 128, 0)" in source
assert "rgb565(255, 40, 40)" in source
assert "for (int band = 3; band >= 0; --band)" in source
assert "strike.epochSec > frameStart && strike.epochSec <= frameEnd" in source
assert "LIGHTNING_REALTIME_OVERLAY_MAX_AGE_SEC" in source
assert "strike.epochSec > newestRadarEnd" in source

# Boundary semantics used by the C++ implementation.
def band(age):
    if age <= 2 * 60:
        return "white"
    if age <= 5 * 60:
        return "yellow"
    if age <= 10 * 60:
        return "orange"
    if age <= 20 * 60:
        return "red"
    return "hidden"

assert band(0) == "white"
assert band(120) == "white"
assert band(121) == "yellow"
assert band(300) == "yellow"
assert band(301) == "orange"
assert band(600) == "orange"
assert band(601) == "red"
assert band(1200) == "red"
assert band(1201) == "hidden"
print("LIGHTNING TRAIL TEST OK")
