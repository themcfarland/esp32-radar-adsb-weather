# Architektura firmware

Projekt je rozdělen do samostatných služeb:

```text
DeviceConfigService  Wi-Fi, AP, NVS, web, OTA, diagnostika
NetworkWorker        fronta runtime HTTP/TLS úloh na core 0
RadarService         ČHMÚ radarová animace
LightningService     LightningMaps WebSocket + bleskový buffer
AdsbService          lokální aircraft.json + adsb.fi + deduplikace
WeatherService       Open-Meteo + volitelně WU
BarometerService     BMP180 + tlaková historie
AstronomyService     Slunce/Měsíc
MapRenderer          mapa, radar, blesky, ADS-B
UI                    LVGL obrazovka, pravý panel, startup/OTA obrazovka
```

## Síťová architektura od 0.30.0

Dlouhé runtime operace DNS/TCP/TLS/HTTP už neběží přímo v hlavním `loop()`. `NetworkWorker` je zpracovává v samostatné FreeRTOS úloze připnuté na **core 0**. Hlavní úloha obsluhuje displej, mapu, dotyk, animaci a přebírá pouze hotové snapshoty.

```text
CORE 1 / hlavní smyčka              CORE 0 / NetworkWorker
-----------------------              ----------------------
LVGL + RGB LCD                       lokální ADS-B HTTP
mapa a radarová animace      <---    adsb.fi HTTPS
LightningMaps WSS                     ČHMÚ radar HTTPS + PNG decode
dotyk a UI                  data     Open-Meteo / WU
barometr a astronomie                forecast
```

Worker provádí **jen jednu klasickou HTTP/HTTPS úlohu současně**. Tím se několik TLS klientů nepere o interní heap a Wi-Fi buffery. Po dokončení se hotová data zkopírují nebo krátce prohodí do aktivní cache v hlavní úloze.

### Chování při výpadku služby

Každý zdroj má vlastní stav a backoff. Při chybě se poslední platná data nemažou:

- lokální ADS-B: krátký backoff, ostatní ADS-B zdroje pokračují,
- adsb.fi: postupně delší retry až do několika minut,
- ČHMÚ radar: poslední animace zůstává na displeji,
- Open-Meteo/WU: poslední počasí/předpověď zůstává viditelné,
- LightningMaps: zůstává samostatný realtime WebSocket s vlastním reconnect/watchdogem.

Runtime Wi-Fi reconnect je také neblokující stavový automat. Jednotlivé uložené profily se zkoušejí postupně bez čekací smyčky v `loop()`. Pokud není dostupný žádný profil, aktivuje se konfigurační AP.

## Ochrana RGB displeje

Od 0.29.4 zůstává jednorázový LCD load guard pro neobvykle dlouhou blokaci hlavní smyčky. Verze 0.30.0 navíc sleduje dokončené síťové úlohy: pokud některá mimořádně dlouhá úloha nebo její chyba zatíží Wi-Fi/PSRAM, může se po dokončení naplánovat jedno odložené srovnání RGB DMA. Recovery má ochranný interval a není periodické.

## Paměť

Velké bloky jsou záměrně směrovány do PSRAM:

- dvojitý mapový framebuffer,
- radarová animace,
- ADS-B HTTP/JSON buffery,
- cache letadel a worker snapshoty,
- LightningMaps JSON/buffer blesků.

Interní heap je ponechán především Wi-Fi/TLS, LVGL, FreeRTOS stackům a systémovým komponentám.

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

Požadavky se plánují přibližně v těchto intervalech; skutečné spuštění může být odloženo frontou nebo backoffem:

- lokální ADS-B: 2 s,
- adsb.fi: 10 s,
- stáří blesků/redraw: 30 s,
- radar: 5 min,
- aktuální počasí: 5 min,
- předpověď: 1 h,
- astronomie: 1 min,
- BMP180: 1 min,
- nový cyklus Wi-Fi reconnectu: přibližně 15 s po neúspěchu všech profilů.

## LightningMaps a recovery sítě (v0.30.5)

LightningMaps WSS již neběží v hlavním Arduino loopu. Samostatný task `lightning-net` na core 0 obsluhuje WebSocket, heartbeat i reconnect. Hlavní loop pouze přebírá příznak nových blesků a kreslí data z chráněného PSRAM bufferu.

Pokud jsou po dobu 3 minut současně stale adsb.fi, LightningMaps a také lokální ADS-B (je-li zapnutý), firmware považuje stav za zaseknutý síťový stack. Pozastaví NetworkWorker, krátce znovu inicializuje Wi-Fi, spustí recovery AP a znovu aktivuje HTTP listener. Cooldown dalšího takového zásahu je 10 minut.
