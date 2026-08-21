#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
config = (root / "include/config.h").read_text()
models = (root / "include/models.h").read_text()
cpp = (root / "src/adsb_service.cpp").read_text()
renderer = (root / "src/map_renderer.cpp").read_text()

assert 'https://opendata.adsb.fi/api' in config
assert 'ADSB_FI_RADIUS_NM = 180' in config
assert 'ADSB_FI_REFRESH_MS = 10UL * 1000UL' in config
assert 'MAX_AIRCRAFT = 180' in config
assert 'fetchAdsbFi' in cpp
assert '"ac"' in cpp
assert 'findAircraftByHex' in cpp
assert 'enrichLocalAircraft' in cpp
assert 'mlatPosition' in cpp and 'source["mlat"]' in cpp
assert 'BasicJsonDocument<PsramAllocator>' in cpp
assert 'adsb.fi' in renderer
assert 'const char* mlat = aircraft.mlatPosition ? " M" : "";' in renderer
assert 'adsbFiCount' in models and 'mlatCount' in models and 'localAircraftCount' in models
print('ADSB.FI HYBRID TEST OK')
