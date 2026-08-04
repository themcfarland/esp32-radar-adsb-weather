# ESP32 radar ČR + ADS-B + počasí

Tato Wiki popisuje firmware pro původní Waveshare ESP32-S3-Touch-LCD-7. Projekt kombinuje radar ČHMÚ, lokální ADS-B data, Weather Underground PWS, Open-Meteo forecast a astronomické výpočty.

Aktuální verze:

```text
0.16.0-touch-map-zoom-nvs
```

## Rychlé odkazy

- [[Installation]]
- [[Hardware]]
- [[Configuration]]
- [[Map-and-Touch]]
- [[Data-Sources]]
- [[Architecture]]
- [[Troubleshooting]]
- [[Release-History]]

## Hlavní ovládání

- `PAUZA/PLAY` zastaví nebo obnoví radarovou animaci.
- `OBNOVIT` okamžitě načte dostupná data.
- Klepnutí do mapy přepíná `celá ČR → 50 km → 25 km → 10 km → celá ČR`.
- Poslední výřez mapy se uloží do NVS a obnoví po restartu.
