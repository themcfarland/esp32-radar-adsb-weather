# Webové nastavení

Web je dostupný na IP adrese zařízení v domácí síti. Při prvním spuštění nebo při problému s Wi-Fi je dostupný na `http://192.168.4.1/` přes konfigurační AP.

## Wi-Fi profily

Lze uložit až **5 Wi-Fi sítí** pro různá místa. Každý řádek obsahuje:

- checkbox **Použít profil**,
- SSID,
- nové heslo.

Povolené profily se při startu postupně zkoušejí a použije se první dostupná síť. Při výpadku Wi-Fi firmware profily znovu prochází; poslední úspěšný profil se zkouší jako první.

Prázdné pole hesla u nezměněného SSID zachová stávající heslo. Při změně SSID znamená prázdné heslo otevřenou síť. Změna Wi-Fi profilů vyvolá restart.

Pokud nejsou dostupné žádné povolené profily, spustí se konfigurační AP `Radar-ADSB-Setup-XXXX` na `192.168.4.1`. Je dovoleno vypnout i všech pět profilů; zařízení pak po restartu zůstane v konfiguračním AP.

Při přechodu z firmware do 0.29.2 se původní jediná Wi-Fi automaticky migruje do profilu 1. Po uložení se kritické Wi-Fi údaje zpětně ověřují čtením z NVS.

## HOME poloha

Uživatel nastaví latitude a longitude v rozsahu české mapy:

```text
lat: 48.30 až 51.30
lon: 11.70 až 19.00
```

HOME se používá pro:

- značku HOME,
- 10km bleskový alarm,
- Open-Meteo,
- astronomii,
- výchozí střed lokálních zoomů.

## Datové zdroje

### Lokální ADS-B přijímač

Přepínač **Používat lokální ADS-B přijímač** určuje, zda se má firmware dotazovat na lokální `aircraft.json`. Při vypnutí zůstane URL uložená, ale nevznikají žádné lokální ADS-B HTTP požadavky; letadla se dál načítají z adsb.fi.

Pokud je lokální přijímač zapnutý a 3 dotazy po sobě selžou, firmware přejde na 30 sekund do backoff režimu. Po úspěšném příjmu se automaticky vrátí k běžnému 2s intervalu.

### Lokální ADS-B URL

Je volitelná. Příklad:

```text
http://192.168.1.100:8080/data/aircraft.json
```

Pokud je prázdná, používá se pouze adsb.fi. Pokud je vyplněná, lokální data mají prioritu a internetový feed doplňuje vzdálenější letadla a MLAT.

### Weather Underground

`WU station` a `WU API key` jsou volitelné. Bez nich funguje aktuální počasí i předpověď přes Open-Meteo. Při dostupném WU firmware preferuje vlastní PWS pro aktuální podmínky.

## Vrstvy mapy

Samostatně lze zapnout/vypnout:

- radar ČHMÚ,
- LightningMaps LIVE,
- ADS-B.

## Zvýraznění letadel

Lze definovat až tři cíle podle callsignu nebo ICAO hex a graficky je zvýraznit na mapě.

## Podsvícení

Každý den týdne může mít vlastní čas zapnutí/vypnutí. Zhasnutý displej lze dočasně probudit dotykem.

## BMP180 a Zambretti

Lze nastavit:

- povolení barometru,
- nadmořskou výšku senzoru,
- jemnou korekci MSL tlaku.

Web obsahuje pomocnou kalibraci nadmořské výšky z aktuálního naměřeného tlaku a referenčního tlaku z dostupného zdroje počasí.

## Servisní funkce

Web nabízí:

- `/diagnostics` – diagnostika,
- `/api/diagnostics` – JSON diagnostika,
- restart zařízení,
- LCD resync,
- factory reset,
- OTA firmware.


## Rozsah mapy kolem HOME

Ve webovem nastaveni lze vedle HOME latitude/longitude zvolit **Cela CR**, **50 km**, **25 km** nebo **10 km**. Po ulozeni se mapa vycentruje na HOME a vyrez se ulozi. Dotykove prepinani zoomu na displeji zustava aktivni.

## Pocasi HOME bez Weather Underground

WU ucet neni potreba. Pokud WU station ID nebo API key nejsou vyplnene, aktualni pocasi i predpoved se automaticky nacitaji z Open-Meteo podle HOME latitude/longitude. WU je pouze volitelny prioritni zdroj vlastni PWS; pri jeho chybe se pouzije Open-Meteo.

## Okamzite prepinani rozsahu mapy

V sekci HOME jsou tlacitka **Cela CR**, **50 km**, **25 km** a **10 km**. Kliknuti se okamzite projevi na LCD, mapa se vycentruje na ulozenou HOME pozici a volba se ulozi do NVS. Neni nutne stisknout hlavni tlacitko Ulozit nastaveni.
