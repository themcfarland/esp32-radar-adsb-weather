from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / "src" / "device_config.cpp").read_text(encoding="utf-8")
hdr = (root / "src" / "device_config.h").read_text(encoding="utf-8")
main = (root / "src" / "main.cpp").read_text(encoding="utf-8")

assert 'server_.on("/map-zoom", HTTP_POST' in cpp
assert 'void DeviceConfigService::handleMapZoom()' in cpp
assert 'void handleMapZoom();' in hdr
assert "class='map-zoom-btn" in cpp
assert "data-mode='0'" in cpp and "data-mode='3'" in cpp
assert "fetch('/map-zoom?mode='" in cpp
assert 'mapZoomRequestPending_ = true;' in cpp
assert 'consumeMapZoomRequested' in main
assert 'saveMapViewport();' in main
print('HOME MAP BUTTONS TEST OK')
