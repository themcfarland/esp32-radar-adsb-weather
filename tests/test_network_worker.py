from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
main = (ROOT / "src/main.cpp").read_text()
worker_h = (ROOT / "src/network_worker.h").read_text()
worker_cpp = (ROOT / "src/network_worker.cpp").read_text()
device_h = (ROOT / "src/device_config.h").read_text()
device_cpp = (ROOT / "src/device_config.cpp").read_text()
radar_h = (ROOT / "src/radar_service.h").read_text()
radar_cpp = (ROOT / "src/radar_service.cpp").read_text()

loop = main.split("void loop()", 1)[1]
assert "xTaskCreatePinnedToCore" in worker_cpp
assert '"network-worker"' in worker_cpp
assert "NetworkWorker::Job::AdsbInternet" in main
assert "NetworkWorker::Job::WeatherCurrent" in main
assert "NetworkWorker::Job::Radar" in main
assert "consumeInternetAdsb" in main
assert "consumeCurrentWeather" in main
assert "consumeRadar" in main
assert "weather.updateCurrent();" not in loop
assert "weather.updateForecast();" not in loop
assert "adsb.update();" not in loop
assert "radar.updateFrames();" not in loop
assert "deviceConfig.serviceNetwork();" in main
assert "ensureNetwork(4000)" not in main
assert "serviceNetwork" in device_h
assert "while (!stationConnected()" not in device_cpp.split("void DeviceConfigService::serviceNetwork()",1)[1].split("void DeviceConfigService::startAccessPoint",1)[0]
assert "RuntimeFrameUpdate" in radar_h
assert "fetchRuntimeUpdate" in radar_cpp
assert "applyRuntimeUpdate" in radar_cpp
assert "NETWORK_LCD_RECOVERY_THRESHOLD_MS" in (ROOT / "include/config.h").read_text()
print("NETWORK WORKER TEST OK")
