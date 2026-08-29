from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
header=(ROOT/"src/adsb_service.h").read_text()
service=(ROOT/"src/adsb_service.cpp").read_text()
worker_h=(ROOT/"src/network_worker.h").read_text()
worker_cpp=(ROOT/"src/network_worker.cpp").read_text()
models=(ROOT/"include/models.h").read_text()
web=(ROOT/"src/device_config.cpp").read_text()
version=(ROOT/"include/version.h").read_text()
assert "0.30.16-adsbfi-110nm" in version
assert "struct NetworkDiagnostics" in header
assert "networkDiagnostics() const" in header
assert "diag.httpCode = code" in service
assert "diag.bodyBytes = body.size" in service
assert "diag.jsonOk = true" in service
assert "diag.apiAircraft = apiArraySize" in service
assert "AdsbService::NetworkDiagnostics adsbFi" in worker_h
assert "diagnostics_.adsbFi = internetAdsbWorker_->networkDiagnostics()" in worker_cpp
assert "maximum = 120000UL" in worker_cpp
for token in ["adsbFiAttempts","adsbFiHttpCode","adsbFiBodyBytes","adsbFiJsonOk","adsbFiApiAircraft","adsbFiLastDurationMs"]: assert token in models
for key in ["adsbfi_attempts","adsbfi_http_code","adsbfi_body_bytes","adsbfi_json_ok","adsbfi_api_aircraft","adsbfi_last_duration_ms","adsbfi_status"]: assert key in web
print("ADSB.FI DIAGNOSTICS TEST OK")
