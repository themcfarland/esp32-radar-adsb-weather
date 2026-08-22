from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
map_cpp = (ROOT / "src/map_renderer.cpp").read_text(encoding="utf-8")
main_cpp = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
weather_cpp = (ROOT / "src/weather_service.cpp").read_text(encoding="utf-8")
config_h = (ROOT / "include/config.h").read_text(encoding="utf-8")

assert "Radar --:-- UTC" not in map_cpp
assert 'strftime(localClock, sizeof(localClock), "%H:%M %Z"' in map_cpp
assert "radar.frameTimeUtc(radarFrame, radarFrameTimeUtc)" in main_cpp
assert 'strftime(output, outputSize, "%H:%M"' in weather_cpp
assert "slotEpoch = times[sourceIndex].as<uint32_t>()" in weather_cpp
assert 'TZ_INFO[] = "CET-1CEST,M3.5.0/2,M10.5.0/3"' in config_h
print("LOCAL TIME DISPLAY TEST OK")
