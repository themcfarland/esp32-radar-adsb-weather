# ESP32 Radar ČR + ADS-B + počasí

Firmware pro **Waveshare ESP32-S3-Touch-LCD-7 (800×480, ST7262, GT911, CH422G, 8 MB flash / 8 MB OPI PSRAM)**. Česká veřejná větev je připravena tak, aby po stažení z GitHubu nebylo nutné upravovat osobní údaje ve zdrojovém kódu.

**Aktuální veřejná verze:** `0.29.0-github-ready-cz`

> Projekt je určen pro původní Waveshare ESP32-S3-Touch-LCD-7. Varianty 7B/7C mohou vyžadovat jiný ovladač displeje.

## Hlavní funkce

- animace radarových snímků **ČHMÚ Open Data**,
- realtime blesky **LightningMaps** jako samostatná vrstva,
- barevné stáří blesků a 10km výstražný kruh kolem HOME,
- ADS-B/MLAT pro celou ČR z **adsb.fi Open Data**,
- volitelný vlastní `aircraft.json` z readsb/dump1090; lokální data mají přednost,
- aktuální počasí a předpověď z **Open-Meteo** bez účtu,
- volitelná vlastní meteostanice z **Weather Underground**,
- BMP180, tlakový trend a Zambretti předpověď,
- Slunce, Měsíc a astronomické údaje,
- dotykový zoom celé ČR / 50 / 25 / 10 km,
- webové nastavení, diagnostika a OTA,
- první konfigurace přes vlastní Wi-Fi AP,
- automatický návrat do konfiguračního AP při nedostupné nebo chybně uložené Wi-Fi.

## První spuštění

1. Nahrajte firmware do desky.
2. Připojte se k síti `Radar-ADSB-Setup-XXXX`.
3. Heslo je `radarsetup`.
4. Otevřete `http://192.168.4.1/`.
5. Zadejte Wi-Fi a vlastní **HOME latitude/longitude**.
6. Volitelně doplňte Weather Underground a lokální ADS-B URL.

Podrobnosti: [[Installation]] a [[Configuration]].

## Co funguje bez účtu

ČHMÚ radar, LightningMaps, adsb.fi, Open-Meteo, astronomie, bleskový alarm, webová diagnostika i OTA fungují bez API klíče. Weather Underground je pouze volitelné rozšíření.

## Datové zdroje

Viz [[Data-Sources]]. Dodržujte podmínky použití jednotlivých poskytovatelů. adsb.fi je v projektu používáno pro osobní/nekomerční zobrazení a attribution zůstává zachován.
