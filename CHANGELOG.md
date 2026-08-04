# Changelog

## 0.16.0 – touch-map-zoom-nvs

- klepnutí do mapy přepíná celou ČR, 50 km, 25 km a 10 km,
- výřez se centruje na místo dotyku,
- režim a střed mapy se ukládají do NVS,
- nastavení se obnoví po restartu,
- radar, hranice, města, stanice i ADS-B používají stejný viewport.

## 0.15.0 – geoboundaries-300pts

- skutečný obrys České republiky z geoBoundaries,
- redukce hranice na 300 bodů vhodných pro ESP32.

## 0.13.0 – openmeteo-http-fix

- Open-Meteo jako hlavní forecast,
- HTTP/1.0 a dekódované tělo odpovědi kvůli chunked transfer,
- předpověď +3, +6, +9, +12, +24 a +48 hodin.

## 0.12.0 – diagnostics

- podrobnější sériové logování,
- heartbeat a diagnostika forecastu.

## 0.9.0 – hourly-48h

- šest hodinových forecast karet do 48 hodin.

## 0.7.0 – RGB stability

- původní 16MHz časování panelu,
- bounce buffer 20 řádků,
- kompaktní radarová cache v PSRAM,
- pomalejší radarová animace pro stabilitu RGB panelu.
