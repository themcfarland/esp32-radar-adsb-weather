from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "src/main.cpp").read_text()
light_cpp = (root / "src/lightning_service.cpp").read_text()
light_h = (root / "src/lightning_service.h").read_text()
dev_cpp = (root / "src/device_config.cpp").read_text()
dev_h = (root / "src/device_config.h").read_text()
config = (root / "include/config.h").read_text()
version = (root / "include/version.h").read_text()

assert "0.30.9-adaptive-tls-guard" in version
assert 'xTaskCreatePinnedToCore(' in light_cpp and '"lightning-net"' in light_cpp
# Main-task poll must not service the WebSocket directly.
loop_start = light_cpp.index("bool LightningService::loop(bool enabled)")
loop_end = light_cpp.index("void LightningService::taskEntry", loop_start)
assert "webSocket_.loop()" not in light_cpp[loop_start:loop_end]
assert "webSocket_.loop()" in light_cpp
assert "setBulkNetworkBusy" in light_h and "setBulkNetworkBusy" in main
assert "forceNetworkRecovery" in dev_h and "forceNetworkRecovery" in dev_cpp
assert "WiFi.disconnect(true, false)" in dev_cpp
assert "server_.begin()" in dev_cpp
assert "NETWORK_GLOBAL_STALE_MS" in config
assert "NETWORK_RECOVERY_COOLDOWN_MS" in config
assert "Network health watchdog" in main
assert "strikeMutex_" in light_h and "xSemaphoreTake(strikeMutex_" in light_cpp
print("NETWORK RECOVERY TEST OK")
