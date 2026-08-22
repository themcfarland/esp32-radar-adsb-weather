from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
adsb_h = (ROOT / "src" / "adsb_service.h").read_text()
adsb = (ROOT / "src" / "adsb_service.cpp").read_text()
light_h = (ROOT / "src" / "lightning_service.h").read_text()
light = (ROOT / "src" / "lightning_service.cpp").read_text()
config = (ROOT / "include" / "config.h").read_text()
main = (ROOT / "src" / "main.cpp").read_text()
version = (ROOT / "include" / "version.h").read_text()

assert "0.30.0-network-worker" in version
assert "AircraftSnapshot* localCache_" in adsb_h
assert "AircraftSnapshot* adsbFiCache_" in adsb_h
assert "MALLOC_CAP_SPIRAM" in adsb
assert "BasicJsonDocument<PsramAllocator>* jsonDoc_" in light_h
assert "new BasicJsonDocument<PsramAllocator>(kJsonCapacity)" in light
assert "ADSB_LOL_BASE_URL" in config
assert "api.adsb.lol" in adsb
assert "opendata.adsb.fi" in adsb
assert "WiFi.hostByName" in adsb
assert "HTTPClient::errorToString" in adsb
assert "runtimeDiagnostics.lastAdsbUpdateMs = adsb.lastSuccessMs();" in main
print("ADSB.FI NETFIX TEST OK")
