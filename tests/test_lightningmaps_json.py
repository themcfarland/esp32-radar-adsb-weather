#!/usr/bin/env python3
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
cpp = (ROOT / "src/lightning_service.cpp").read_text()
hdr = (ROOT / "src/lightning_service.h").read_text()

sample = r'''{"time":1786977512,"flags":{"2":0},"strokes":[{"time":1786977509924,"lat":44.52783,"lon":9.426126,"src":2,"srv":1,"id":23672676,"del":1807,"dev":507},{"time":1786977510039,"lat":44.54155,"lon":9.425901,"src":2,"srv":1,"id":23672677,"del":1763,"dev":813},{"time":1786977510225,"lat":46.177468,"lon":12.54539,"src":2,"srv":1,"id":23672678,"del":1819,"dev":1824},{"time":1786977510249,"lat":50.509224,"lon":18.160387,"src":2,"srv":1,"id":23672679,"del":1825,"dev":435}]}'''
data = json.loads(sample)
assert data["time"] == 1786977512
assert len(data["strokes"]) == 4
assert data["strokes"][3]["id"] == 23672679
assert data["strokes"][3]["time"] // 1000 == 1786977510
assert abs(data["strokes"][3]["lat"] - 50.509224) < 1e-8
assert abs(data["strokes"][3]["lon"] - 18.160387) < 1e-8

assert 'live2.lightningmaps.org' in cpp
assert 'from_lightningmaps_org' in cpp
assert 'p\\\":[%.2f,%.2f,%.2f,%.2f]' in cpp
assert 'deserializeJson' in cpp and 'DeserializationOption::Filter' in cpp
assert 'timeMs / 1000ULL' in cpp
assert 'uint32_t id = 0;' in hdr
assert 'decodeHeaderLzw' not in cpp
assert 'ws7.blitzortung.org' not in cpp
print('LIGHTNINGMAPS JSON TEST OK')
