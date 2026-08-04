# Konfigurace

## Soubor secrets.h

Soubor vytvořte z `include/secrets.example.h`:

```cpp
#define WIFI_SSID          "NAZEV_WIFI"
#define WIFI_PASSWORD      "HESLO_WIFI"
#define WU_API_KEY         "WEATHER_UNDERGROUND_API_KEY"
#define WU_STATION_ID      "IPLZE179"
#define ADSB_AIRCRAFT_URL  "http://192.168.1.100:8080/data/aircraft.json"
```

`include/secrets.h` je v `.gitignore`.

## Hlavní konstanty v config.h

- rozlišení a rozměry panelů,
- geografický rozsah celé mapy ČR,
- kalibrace radarového PNG,
- interval ADS-B 2 s,
- radar 5 min,
- PWS 5 min,
- forecast 1 h,
- astronomické údaje 1 min,
- maximální počet letadel 80.

## Záložní souřadnice

Pokud PWS nevrátí souřadnice, firmware použije `FALLBACK_LAT` a `FALLBACK_LON` v `include/config.h`.

## Časové pásmo

Firmware používá české časové pásmo s automatickým přechodem CET/CEST:

```text
CET-1CEST,M3.5.0/2,M10.5.0/3
```
