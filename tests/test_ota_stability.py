#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
dev = (ROOT / "src/device_config.cpp").read_text(encoding="utf-8")
main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")

assert "server_.hasArg(\"size\")" in dev
assert "Update.begin(updateSize, U_FLASH)" in dev
assert "otaBytesWritten_ != otaExpectedBytes_" in dev
start = dev.index("if (upload.status == UPLOAD_FILE_WRITE)")
end = dev.index("if (upload.status == UPLOAD_FILE_END)", start)
write_block = dev[start:end]
assert "otaDisplayCallback_" not in write_block
assert "lv_refr_now" not in write_block
assert "esp_lcd_rgb_panel_restart" not in write_block
p0 = main.index("case OtaDisplayEvent::Progress:")
p1 = main.index("case OtaDisplayEvent::Success:", p0)
progress = main[p0:p1]
assert "setOtaBacklightRaw(false)" in progress
assert "lv_refr_now" not in progress
assert "lvgl_port_lock" not in progress
assert "esp_lcd_rgb_panel_restart" not in progress
assert "UI::" not in progress
print("OTA STABILITY TEST OK")

assert 'server_.on("/ota-prepare"' in dev
assert "otaPrepareDisplayPending_" in dev
assert "Update.begin OK, waiting for firmware chunks" in dev
assert "OTA: first chunk" in dev
assert "lv_refr_now(display)" not in main[main.index("case OtaDisplayEvent::Start:"):main.index("case OtaDisplayEvent::Progress:")]

assert "OTA: reboot armed after successful Update.end" in dev
assert "restartAt_ = millis() + 2200U" in dev
assert "OTA: success response, reboot in 1 s" in dev
assert "setOtaBacklightRaw(false)" in main
assert "otaDisplayFailurePending_" in dev
print("OTA REBOOT/BLACKOUT TEST OK")
