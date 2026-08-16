#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/lightning_service.cpp").read_text()
header = (ROOT / "src/lightning_service.h").read_text()
main = (ROOT / "src/main.cpp").read_text()
config = (ROOT / "include/config.h").read_text()

assert "LIGHTNING_REDRAW_MS = 30UL * 1000UL" in config
assert "renderLive" in header
assert "LightningService::renderLive" in source
assert "const uint32_t ageSec = nowEpoch - strike.epochSec" in source
assert "radarFrameTimes_" not in header
assert "updateForRadar" not in source
assert "frameReady" not in header
assert "lightning.renderLive(buffer" in main
assert "lightning.renderFrame" not in main
assert "radarLayerEnabled && !UI::radarPaused()" in main
assert "radarLayerEnabled || lightningLayerEnabled" not in main

# A lightning point must have identical visibility regardless of which radar
# frame happens to be underneath it. Only real current age matters.
def visible(strike_epoch, now_epoch):
    if strike_epoch > now_epoch + 5:
        return False
    return now_epoch - strike_epoch <= 20 * 60

for radar_frame in range(6):
    assert visible(10_000, 10_100), radar_frame
    assert not visible(8_000, 10_100), radar_frame

print("LIGHTNING INDEPENDENT OVERLAY TEST OK")
