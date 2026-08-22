from pathlib import Path

root = Path(__file__).resolve().parents[1]
dev_h = (root / "src/device_config.h").read_text(encoding="utf-8")
dev = (root / "src/device_config.cpp").read_text(encoding="utf-8")
main = (root / "src/main.cpp").read_text(encoding="utf-8")
weather = (root / "src/weather_service.cpp").read_text(encoding="utf-8")
models = (root / "include/models.h").read_text(encoding="utf-8")

assert "home_map_zoom" in dev
assert "consumeMapZoomRequested" in dev_h
assert "mapZoomRequestPending_" in dev_h
assert "MapRenderer::makeViewport(" in main and "requestedMapZoom" in main
assert "runtimeDiagnostics.mapZoomMode" in main
assert "uint8_t mapZoomMode = 0" in models
assert "Current weather: WU not configured, using Open-Meteo" in weather
assert "fetchOpenMeteoCurrent" in weather
assert "weather.setLocation(homeLat, homeLon)" in main
print("HOME MAP + WEATHER TEST OK")
