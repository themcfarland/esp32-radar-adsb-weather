from pathlib import Path

root = Path(__file__).resolve().parents[1]
config = (root / "include" / "config.h").read_text()
version = (root / "include" / "version.h").read_text()
weather = (root / "src" / "weather_service.cpp").read_text()

assert "0.30.16-adsbfi-110nm" in version
assert "FORECAST_REFRESH_MS = 2UL * 60UL * 60UL * 1000UL" in config
assert "CURRENT_WEATHER_REFRESH_MS = 5UL * 60UL * 1000UL" in config
assert "once every two hours" in weather
print("FORECAST 2H TEST OK")
