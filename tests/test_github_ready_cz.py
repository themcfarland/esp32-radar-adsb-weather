from pathlib import Path
root=Path(__file__).resolve().parents[1]
config=(root/'include/config.h').read_text()
defaults=(root/'include/settings_defaults.h').read_text()
device=(root/'src/device_config.cpp').read_text()
weather=(root/'src/weather_service.cpp').read_text()
main=(root/'src/main.cpp').read_text()
assert '#define WU_STATION_ID ""' in defaults
assert '#define ADSB_AIRCRAFT_URL ""' in defaults
assert 'home_lat' in device and 'home_lon' in device
assert 'settings_.homeLat' in device and 'settings_.homeLon' in device
assert 'fetchOpenMeteoCurrent' in weather
assert 'weather.setLocation(homeLat, homeLon)' in main
assert 'recentStrikeWithin(homeLat, homeLon' in main
assert 'DEFAULT_HOME_LAT' in config and 'DEFAULT_HOME_LON' in config
print('GITHUB READY CZ TEST OK')
assert 'homeChanged' in main
assert 'outdoor temperature history cleared after weather location/source change' in main
