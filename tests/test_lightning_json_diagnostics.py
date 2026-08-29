from pathlib import Path

cpp = Path("src/lightning_service.cpp").read_text(encoding="utf-8")
h = Path("src/lightning_service.h").read_text(encoding="utf-8")
models = Path("include/models.h").read_text(encoding="utf-8")
device = Path("src/device_config.cpp").read_text(encoding="utf-8")

assert "Lightning JSON control" in cpp
assert "jsonControlMessages_" in cpp and "jsonControlMessages()" in h
assert "insideLightningMap" in cpp
assert "strokesOutsideMap_" in cpp and "strokesDuplicates_" in cpp and "strokesInvalid_" in cpp
assert "StaticJsonDocument<512> filter" in cpp
assert "payload sample" in cpp
assert "lightningJsonControlMessages" in models
assert "lightning_strokes_outside_map" in device
assert "lightning_strokes_duplicates" in device
assert "lightning_strokes_invalid" in device
print("lightning JSON diagnostics tests: OK")
