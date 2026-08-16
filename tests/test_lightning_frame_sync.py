#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/lightning_service.cpp").read_text()
config = (ROOT / "include/config.h").read_text()

assert "LIGHTNING_REALTIME_OVERLAY_MAX_AGE_SEC = 5UL * 60UL" in config
assert "strike.epochSec > frameStart && strike.epochSec <= frameEnd" in source
assert "latestFrame && clockValid && strike.epochSec > newestRadarEnd" in source
assert "realtimeAge <= Config::LIGHTNING_REALTIME_OVERLAY_MAX_AGE_SEC" in source

STEP = 5 * 60
LIVE = 5 * 60

def belongs_to_slot(strike, frame_end):
    return frame_end - STEP < strike <= frame_end

# A strike must belong to one and only one adjacent radar slot.
assert belongs_to_slot(1000, 1100)
assert not belongs_to_slot(1000, 1400)
assert belongs_to_slot(1100, 1100)
assert not belongs_to_slot(1100, 1400)
assert not belongs_to_slot(800, 1100)  # lower boundary is open

# Realtime extension is only short-lived and only newer than latest radar data.
def realtime_visible(strike, latest_radar_end, now):
    if not (strike > latest_radar_end and strike <= now + 5):
        return False
    age = max(0, now - strike)
    return age <= LIVE

assert realtime_visible(2001, 2000, 2200)
assert realtime_visible(2199, 2000, 2200)
assert not realtime_visible(1899, 2000, 2200)
assert not realtime_visible(2001, 2000, 2402)

print("LIGHTNING FRAME SYNC TEST OK")
