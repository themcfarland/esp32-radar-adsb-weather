from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
cpp = (ROOT / "src" / "adsb_service.cpp").read_text()
h = (ROOT / "src" / "adsb_service.h").read_text()
main = (ROOT / "src" / "main.cpp").read_text()
version = (ROOT / "include" / "version.h").read_text()

assert "0.29.3-wifi-profiles" in version
assert "downloadJsonBody" in cpp
assert "truncated HTTP body" in cpp
assert "body complete" in cpp
assert "reinterpret_cast<char*>(body.data)" in cpp
assert "bool update(bool includeNetwork = true)" in h
assert "adsb.update(false);" in main
assert "adsb.fi starts after dashboard" in main
print("ADSB.FI BUFFERED TEST OK")
