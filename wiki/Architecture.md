# Architektura

## Moduly

| Modul | Úloha |
|---|---|
| `main.cpp` | inicializace, časování, Wi-Fi, NVS a řízení aktualizací |
| `radar_service` | seznam radarů, stažení PNG, PNGdec, cache a projekce |
| `adsb_service` | HTTP načtení a parsování letadel |
| `weather_service` | PWS, Open-Meteo a WU/TWC |
| `astronomy_service` | Slunce, Měsíc, východy, západy a fáze |
| `map_renderer` | základ mapy, hranice, města, grid a symboly letadel |
| `ui` | LVGL objekty, buffery, tlačítka a mapové dotyky |

## Paměť

Projekt používá PSRAM pro:

- dva RGB565 mapové buffery,
- kompaktní radarovou cache,
- dočasný buffer dekódovaného PNG,
- ikony forecastu a Měsíce.

## Dvojité bufferování

Jedna mapa se zobrazuje, zatímco druhá se připravuje. Po dokončení se canvasy prohodí, takže uživatel nevidí postupné kreslení jednotlivých vrstev.

## Stabilita RGB panelu

Projekt zachovává původní 16MHz pixel clock a při buildu zvětšuje RGB bounce buffer na 20 řádků. Radarová animace používá cache a interval 1400 ms, aby se omezila zátěž PSRAM a GDMA.
