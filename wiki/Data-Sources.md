# Datové zdroje

## ČHMÚ Open Data – radar

Radarová animace používá česká radarová data ČHMÚ. Firmware pracuje s 5minutovým krokem a drží šest snímků v PSRAM. Během runtime se radarová PNG data neukládají do LittleFS.

## LightningMaps – blesky

Realtime vrstva používá WebSocket:

```text
wss://live2.lightningmaps.org/
```

Po připojení firmware pošle viewport české mapy a přijímá plain JSON se seznamem `strokes`. Používají se především:

```text
time, lat, lon, id
```

Blesky jsou nezávislé na radarové animaci.

Barevné stáří:

- 0–2 min: bílý blesk,
- 2–5 min: žlutá,
- 5–10 min: oranžová,
- 10–20 min: červená,
- >20 min: odstranění.

Pokud je blesk mladší než 10 minut do 10 km od HOME, aktivuje se červený geografický 10km kruh.

> LightningMaps/Blitzortung realtime rozhraní není garantované stabilní veřejné API. Firmware proto obsahuje reconnect a watchdog.

## adsb.fi – ADS-B/MLAT

Internetový provoz pro celou ČR se načítá z adsb.fi. Výřez je centrován přibližně na střed ČR s poloměrem 180 NM. Firmware parsuje jen údaje potřebné pro mapu a rozpoznává MLAT pozice.

Lokální `aircraft.json` má při duplicitním ICAO přednost. adsb.fi doplní provoz mimo dosah vlastní antény.

## Open-Meteo

Bez účtu poskytuje:

- aktuální počasí,
- předpověď +3 / +6 / +9 h,
- meteorologické vstupy použitelné pro pravý panel a Zambretti podle dostupnosti.

Dotaz se sestavuje podle uživatelské HOME polohy.

## Weather Underground

Volitelný zdroj vlastní PWS. Vyžaduje station ID a API key. Pokud není nakonfigurován nebo selže, firmware zůstává použitelný přes Open-Meteo.

## Lokální BMP180

Není internetovým zdrojem. Měří místní tlak a teplotu senzoru a používá se pro tlakový trend a lokální Zambretti výpočet.
