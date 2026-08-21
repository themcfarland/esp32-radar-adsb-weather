# Architektura firmware

Projekt je rozdělen do samostatných služeb:

```text
DeviceConfigService  Wi-Fi, AP, NVS, web, OTA, diagnostika
RadarService         ČHMÚ radarová animace
LightningService     LightningMaps WebSocket + bleskový buffer
AdsbService          lokální aircraft.json + adsb.fi + deduplikace
WeatherService       Open-Meteo + volitelně WU
BarometerService     BMP180 + tlaková historie
AstronomyService     Slunce/Měsíc
MapRenderer          mapa, radar, blesky, ADS-B
UI                    LVGL obrazovka, pravý panel, startup/OTA obrazovka
```

## Paměť

Velké bloky jsou záměrně směrovány do PSRAM:

- dvojitý mapový framebuffer,
- radarová animace,
- ADS-B HTTP/JSON buffery,
- cache letadel,
- LightningMaps JSON/buffer blesků.

Interní heap je ponechán především Wi-Fi/TLS, LVGL a systémovým komponentám.

## Flash partitions

Projekt používá dvě OTA partitions:

```text
nvs      0x5000
otadata  0x2000
app0     0x2F0000
app1     0x2F0000
spiffs   0x210000
```

## Periodické aktualizace

Přibližné intervaly:

- lokální ADS-B: 2 s,
- adsb.fi: 10 s,
- stáří blesků/redraw: 30 s,
- radar: 5 min,
- aktuální počasí: 5 min,
- předpověď: 1 h,
- astronomie: 1 min,
- BMP180: 1 min,
- Wi-Fi reconnect: 15 s.
