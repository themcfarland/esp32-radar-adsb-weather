from pathlib import Path
root=Path(__file__).resolve().parents[1]
config=(root/'include/config.h').read_text()
device_h=(root/'src/device_config.h').read_text()
device=(root/'src/device_config.cpp').read_text()
adsb_h=(root/'src/adsb_service.h').read_text()
adsb=(root/'src/adsb_service.cpp').read_text()
main=(root/'src/main.cpp').read_text()
assert 'bool localAdsbEnabled = false' in device_h
assert 'adsb_local_enabled' in device and 'adsb_local_on' in device
assert 'setLocalEnabled(settings.localAdsbEnabled)' in main
assert 'ADSB_LOCAL_BACKOFF_AFTER_FAILURES = 3' in config
assert 'ADSB_LOCAL_FAILURE_BACKOFF_MS = 30UL * 1000UL' in config
assert 'consecutiveLocalFailures_' in adsb_h
assert 'retry in %u s' in adsb
assert 'localEnabled_ && localCache_' in adsb
print('ADSB LOCAL CONTROL TEST OK')
