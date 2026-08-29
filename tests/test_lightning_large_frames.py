#!/usr/bin/env python3
"""Regression checks for LightningMaps large initial WebSocket batches."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
pio = (ROOT / "platformio.ini").read_text()
hdr = (ROOT / "src" / "lightning_service.h").read_text()
cpp = (ROOT / "src" / "lightning_service.cpp").read_text()
patch = (ROOT / "scripts" / "patch_websockets_psram.py").read_text()
device = (ROOT / "src" / "device_config.cpp").read_text()
version = (ROOT / "include" / "version.h").read_text()

assert "0.30.16-adsbfi-110nm" in version
assert "pre:scripts/patch_websockets_psram.py" in pio
assert "WEBSOCKETS_MAX_DATA_SIZE=32768" not in pio
assert "MAX_RX_KIB = 192" in patch
assert "heap_caps_malloc" in patch
assert "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT" in patch
assert "LIGHTNINGMAPS_PSRAM_RX_PATCH" in patch
assert "kJsonCapacity = 128U * 1024U" in hdr
assert "WStype_FRAGMENT_TEXT_START" in cpp
assert "WStype_FRAGMENT_FIN" in cpp
assert "heap_caps_realloc" in cpp
assert "server hello cid=" in cpp
assert "Lightning RX:" in cpp
assert "Do not trust the subscription viewport" in cpp
for key in (
    "lightning_last_frame_bytes",
    "lightning_largest_frame_bytes",
    "lightning_json_messages",
    "lightning_json_errors",
    "lightning_strokes_received",
    "lightning_strokes_accepted",
    "lightning_disconnect_count",
):
    assert key in device

# Reproduce the important property of the browser capture: a plausible initial
# batch is much larger than the upstream 15 KiB limit and the old 32 KiB idea.
strokes = [
    {
        "time": 1787945972617 + i,
        "lat": 45.0 + (i % 50) * 0.01,
        "lon": 12.0 + (i % 70) * 0.01,
        "src": 2,
        "srv": 1,
        "id": 1_300_000 + i,
        "del": 1800,
        "dev": 5000,
    }
    for i in range(500)
]
payload = json.dumps({"time": 1787946243, "flags": {"2": 2}, "strokes": strokes}, separators=(",", ":"))
assert len(payload) > 32 * 1024, len(payload)
assert len(payload) < 192 * 1024, len(payload)
print(f"LIGHTNING LARGE FRAME TEST OK: synthetic 500-stroke frame = {len(payload)} B")
