from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / "src" / "adsb_service.cpp").read_text()
version = (root / "include" / "version.h").read_text()
assert "0.30.0-network-worker" in version
assert 'downloadJsonBody(http, "ADSB local", contentLength, body)' in src
assert 'reinterpret_cast<char*>(body.data), body.size' in src
assert 'http.useHTTP10(true)' not in src[src.index('bool AdsbService::fetchLocal'):src.index('bool AdsbService::fetchNetworkProvider')]
assert 'http.setTimeout(10000)' in src
assert 'ADSB local: parsed %u aircraft from %u B' in src
print("ADSB LOCAL BUFFERED TEST OK")
