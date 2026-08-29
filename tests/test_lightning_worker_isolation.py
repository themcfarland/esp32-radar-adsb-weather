from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
cpp = (ROOT / "src/lightning_service.cpp").read_text()
h = (ROOT / "src/lightning_service.h").read_text()
main = (ROOT / "src/main.cpp").read_text()
version = (ROOT / "include/version.h").read_text()

assert "0.30.16-adsbfi-110nm" in version
assert 'xTaskCreatePinnedToCore(' in cpp
assert '"lightning-wss"' in cpp
assert 'workerTaskLoop()' in cpp
assert 'serviceSocketOnce()' in cpp
assert 'if (socketStarted_) webSocket_.loop();' in cpp
# Main Arduino loop must only call the facade, never WebSocketsClient::loop directly.
main_loop = main[main.index("void loop()") :]
assert "webSocket_.loop()" not in main_loop
assert "lightning.loop(false);" in main_loop
assert "kMaxJsonParseBytes = 32U * 1024U" in h
assert "skipped large history frame" in cpp
assert "std::atomic<bool> enabledRequested_" in h
assert "SemaphoreHandle_t strikeMutex_" in h
print("LIGHTNING WORKER ISOLATION TEST OK")
