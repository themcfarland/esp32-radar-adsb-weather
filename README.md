# Waveshare 7" Radar ČR + ADS-B + počasí

Firmware **0.29.0-github-ready-cz** je veřejná česká varianta projektu pro
**Waveshare ESP32-S3-Touch-LCD-7 (800×480, ST7262, GT911, 8 MB OPI PSRAM)**.
Po stažení z GitHubu neobsahuje osobní Wi-Fi, Weather Underground stanici ani
lokální IP adresu ADS-B přijímače.

## První spuštění

1. Nahrajte firmware do desky.
2. Připojte se k AP `Radar-ADSB-Setup-XXXX`, heslo `radarsetup`.
3. Otevřete `http://192.168.4.1/`.
4. Zadejte Wi-Fi a **HOME latitude/longitude**.
5. Ostatní zdroje jsou volitelné a nastavení se uloží do NVS.

> Při aktualizaci ze starší větve 0.28.x zůstane staré NVS zachováno, ale
> souřadnice HOME dříve nebyly uživatelským nastavením. Po prvním spuštění
> verze 0.29.0 proto zkontrolujte a uložte vlastní latitude/longitude ve webu.

## Co funguje bez účtu

- radarová animace **ČHMÚ Open Data** pro ČR,
- realtime blesky **LightningMaps** přes plain-JSON WSS,
- ADS-B/MLAT pro celou ČR z **adsb.fi Open Data**,
- aktuální počasí a +3/+6/+9 h předpověď z **Open-Meteo**,
- Slunce, Měsíc a astronomické údaje pro uživatelskou HOME pozici,
- 10km realtime bleskový alarm kolem HOME,
- webové nastavení, diagnostika a OTA.

## Volitelné zdroje

### Lokální ADS-B
Pole `Lokalni ADS-B URL` může zůstat prázdné. Pak se používá pouze adsb.fi.
Pokud uživatel provozuje readsb/dump1090, může zadat například:

```text
http://192.168.1.100:8080/data/aircraft.json
```

Lokální data mají prioritu a adsb.fi doplní vzdálenější ADS-B/MLAT provoz.

### Weather Underground
WU station ID a API key jsou volitelné. Pokud nejsou vyplněné, aktuální počasí
i předpověď používají Open-Meteo. Pokud jsou vyplněné, firmware preferuje
aktuální měření vlastní PWS a při chybě automaticky použije Open-Meteo.

## HOME poloha

Latitude/longitude se nastavují ve webu a ukládají do NVS. Používají se pro:

- značku HOME na mapě,
- 10km bleskový výstražný kruh,
- Open-Meteo,
- astronomické výpočty,
- výchozí střed mapy před uložením vlastního zoomu.

Firmware je určen pro Českou republiku; mapa, ČHMÚ radar a časová zóna
`CET/CEST` jsou pevně české.

## Blesky

LightningMaps je nezávislá realtime vrstva nad radarovou animací. Barevné stáří:

- 0–2 min: bílý blesk,
- 2–5 min: žlutá,
- 5–10 min: oranžová,
- 10–20 min: červená,
- starší záznamy se odstraní.

## OTA

Web OTA používá dvojici OTA partitions. Před zápisem se zobrazí jednoduchá
OTA obrazovka, při zápisu se vypne podsvícení a po úspěšném `Update.end(true)`
se zařízení automaticky restartuje. NVS nastavení zůstává zachováno.

## Barometr

Podporovaný senzor je BMP180 na sdílené I2C0:

```text
BMP180        Waveshare
VCC           3V3
GND           GND
SDA           GPIO8
SCL           GPIO9
```

Nadmořská výška a korekce tlaku se nastavují ve webu. Zambretti používá místní
barometrický trend počítaný lineární regresí z posledních přibližně 3 hodin; venkovní teplota a směr větru mohou pocházet z WU nebo
Open-Meteo podle dostupnosti.

## Sestavení

Projekt je připraven pro PlatformIO. Použijte environment:

```text
waveshare-esp32-s3-touch-lcd-7
```

Před zveřejněním vlastního forku nevkládejte API klíče do repozitáře; případné
compile-time hodnoty patří do lokálního `include/secrets.h`, který není potřeba
pro běžné první spuštění.

## Datové zdroje

- ČHMÚ Open Data – radar,
- LightningMaps / Blitzortung – blesková aktivita,
- adsb.fi – veřejná ADS-B/MLAT data pro osobní/nekomerční použití,
- Open-Meteo – počasí a předpověď,
- Weather Underground – volitelná PWS data.

Dodržujte podmínky použití jednotlivých poskytovatelů dat.
