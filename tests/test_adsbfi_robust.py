from pathlib import Path
root = Path(__file__).resolve().parents[1]
cpp = (root / "src/adsb_service.cpp").read_text()
h = (root / "src/adsb_service.h").read_text()
version = (root / "include/version.h").read_text()
assert "0.28.20-adsb-local-buffered" in version
assert "640U * 1024U" in cpp
assert "client.setTimeout(15000)" in cpp
assert "http.setTimeout(15000)" in cpp
network = cpp.split("bool AdsbService::fetchNetworkProvider",1)[1].split("bool AdsbService::fetchAdsbFi",1)[0]
assert "http.useHTTP10(true)" not in network
assert "total=%u ac=%u accepted=%u" in cpp
assert "seen = 0.0f" in cpp
assert "adsbFiStatus_" in h
print("ADSB.FI ROBUST TEST OK")
