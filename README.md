# Waveshare 7" Radar ČR + ADS-B + počasí

Firmware **0.30.2-home-map-weather** je veřejná česká varianta projektu pro
**Waveshare ESP32-S3-Touch-LCD-7 (800×480, ST7262, GT911, 8 MB OPI PSRAM)**.
Po stažení z GitHubu neobsahuje osobní Wi-Fi, Weather Underground stanici ani
lokální IP adresu ADS-B přijímače.

## Nová síťová architektura v0.30.0

Od verze **0.30.0-network-worker** neběží runtime DNS/TCP/TLS/HTTP operace
v hlavní Arduino smyčce. Samostatný FreeRTOS **NetworkWorker na core 0**
seriově zpracovává:

- lokální `aircraft.json`,
- adsb.fi / adsb.lol,
- aktuální počasí,
- ČHMÚ radarový update,
- hodinovou předpověď.

V jednu chvíli se provádí pouze **jedna klasická HTTP/HTTPS úloha**. Hotová
data se předají hlavnímu vláknu až po dokončení přenosu a parsování. LVGL,
animace mapy, dotyk a hodiny proto pokračují i při timeoutu vzdáleného serveru.
LightningMaps zůstává jako lehký realtime WebSocket obsluhovaný průběžně.

Při chybě služby se zachovají poslední platná data a pro další pokusy se použije
postupný backoff. Runtime reconnect Wi-Fi je nově **neblokující stavový automat**:
pět uložených profilů se zkouší bez čekacích `while/delay` smyček v hlavním
programu a při neúspěchu se dál spustí konfigurační AP.

Webová diagnostika navíc ukazuje aktivní síťovou úlohu, počet čekajících úloh,
čas poslední a nejdelší úlohy, počet chyb a backoffů. Pokud je background
operace mimořádně dlouhá nebo selže, může se po dokončení naplánovat jeden
RGB DMA resync; platí stejný minimální 90s cooldown jako u display load guardu.

## První spuštění

1. Nahrajte firmware do desky.
2. Připojte se k AP `Radar-ADSB-Setup-XXXX`, heslo `radarsetup`.
3. Otevřete `http://192.168.4.1/`.
4. Nastavte až 5 Wi-Fi profilů (SSID/heslo + přepínač Použít) a **HOME latitude/longitude**.
5. Ostatní zdroje jsou volitelné a nastavení se uloží do NVS.

> Při aktualizaci ze starší větve 0.28.x zůstane staré NVS zachováno, ale
> souřadnice HOME dříve nebyly uživatelským nastavením. Po prvním spuštění
> verze 0.29.0 proto zkontrolujte a uložte vlastní latitude/longitude ve webu.

## Automatické srovnání LCD při vysoké zátěži

Display load guard zůstává jako druhá ochranná vrstva. Hlavní smyčka sleduje
vlastní neobvykle dlouhé blokace a NetworkWorker samostatně sleduje délku a
úspěch síťových úloh. Resync se **neprovádí periodicky**: naplánuje se jen po
výjimečně dlouhé (>= 8 s) nebo chybové background operaci, případně při dlouhé
blokaci hlavního loopu. Mezi automatickými opravami je minimálně 90 s. Ruční
tlačítko **Srovnat LCD** ve webu zůstává dostupné.

## Více Wi-Fi míst

Ve webovém nastavení lze uložit až **5 Wi-Fi profilů**. Každý profil má vlastní SSID, heslo a checkbox **Použít profil**. Firmware při startu i při pozdějším výpadku sítě postupně zkouší všechny povolené profily a připojí se k první dostupné síti. Poslední úspěšný profil se při dalším reconnectu zkouší jako první.

Pokud není dostupný žádný povolený profil, zařízení spustí failsafe AP `Radar-ADSB-Setup-XXXX` na `192.168.4.1`. Starší jedno-síťové nastavení z verzí do 0.29.2 se při prvním startu automaticky převede do **profilu 1**. Kritické Wi-Fi údaje se po uložení zpětně načtou z NVS a ověří.

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
Ve webovém nastavení je přepínač **Používat lokální ADS-B přijímač**. Je-li vypnutý, firmware lokální `aircraft.json` vůbec nedotazuje a používá pouze adsb.fi. URL může zůstat uložená pro pozdější použití.

Při zapnutém lokálním přijímači se po **3 po sobě jdoucích chybách** aktivuje **30s backoff**, takže nedostupný receiver nezatěžuje hlavní smyčku opakovaným požadavkem každé 2 s.

Pole `Lokalni ADS-B URL` může zůstat prázdné, pokud je lokální přijímač vypnutý. Pak se používá pouze adsb.fi.
Pokud uživatel provozuje readsb/dump1090, může zadat například:

```text
http://192.168.1.100:8080/data/aircraft.json
```

Lokální data mají prioritu a adsb.fi doplní vzdálenější ADS-B/MLAT provoz.

### Počasí HOME / Weather Underground
Weather Underground není pro provoz nutný. Pokud uživatel nemá WU účet nebo ponechá WU station ID / API key prázdné, firmware automaticky použije **Open-Meteo podle GPS souřadnic HOME**. Získává tak aktuální teplotu, relativní vlhkost, tlak, srážky, rychlost a směr větru, nárazy větru a předpověď +3/+6/+9 hodin.

Pokud jsou WU station ID a API key vyplněné, firmware preferuje aktuální měření vlastní PWS. Když WU selže, automaticky přejde na Open-Meteo pro HOME souřadnice.

## HOME poloha a mapový rozsah

Latitude/longitude se nastavují ve webu a ukládají do NVS. Ve stejné kartě lze zvolit také výchozí/aktuální rozsah mapy kolem HOME: **celá ČR / 50 km / 25 km / 10 km**. Po uložení se mapa vycentruje na HOME a mapový výřez se uloží do existujícího `mapview` NVS. Dotykové přepínání zoomu na LCD zůstává zachováno.

HOME latitude/longitude se používají pro:

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


### v0.29.6

Hlavni LCD obrazovka uz nezobrazuje tlacitka **PAUZA** a **OBNOVIT**. Radarova animace a obnovovani dat probiha automaticky.
