## 0.30.3-home-map-buttons

- tlacitka Cela CR / 50 / 25 / 10 km ve webu
- okamzite prekresleni LCD mapy kolem HOME bez odeslani celeho formulare

## 0.30.2-home-map-weather

- mapovy rozsah HOME lze nastavit z webu: CR / 50 / 25 / 10 km,
- ulozeni recenteruje mapu na HOME a zachova volbu v NVS,
- zvyraznen Open-Meteo HOME fallback bez WU uctu.

## 0.30.0 – Network worker

- runtime DNS/TCP/TLS/HTTP operace přesunuty z hlavního `loop()` do samostatného FreeRTOS workeru na core 0,
- velké HTTP/HTTPS úlohy se provádějí sériově, vždy pouze jedna současně,
- per-service backoff při výpadku a zachování posledních platných dat,
- neblokující runtime reconnect přes až 5 Wi-Fi profilů + failsafe AP,
- radarový runtime download/decode probíhá do odděleného PSRAM overlaye a do aktivní animace se vloží krátkým swapem,
- rozšířená webová diagnostika síťového workeru,
- LCD recovery guard reaguje i na mimořádně dlouhou/chybovou síťovou úlohu.

## 0.29.6 – Screen cleanup

- odstraněna LCD tlačítka **PAUZA** a **OBNOVIT**,
- radar a datové zdroje se dál aktualizují automaticky,
- rozšířen prostor pro stavový text v horní liště.

## 0.29.5-local-time

- všechny uživatelsky zobrazené hodinové časy sjednoceny na CET/CEST
- radar ČHMÚ: UTC timestamp -> lokální čas na mapě
- předpovědní karty: lokální čas forecast slotu

# Release history

## 0.29.4-display-load-guard
- automatické jednorázové srovnání LCD po dlouhé blokující operaci,
- 90s ochranný interval mezi automatickými zásahy,
- diagnostika zátěže LCD recovery guardu.

# Historie verzí

## 0.29.3-wifi-profiles

- až 5 uložených Wi-Fi profilů s individuálním checkboxem Použít,
- automatický výběr první dostupné povolené sítě,
- reconnect postupně přes všechny profily a preference posledního úspěšného,
- automatická migrace původního jednoho SSID do profilu 1,
- read-back kontrola kritických Wi-Fi údajů po zápisu do NVS,
- failsafe konfigurační AP, pokud není dostupný žádný profil.

## 0.29.2-local-adsb-control

- samostatné zapnutí/vypnutí lokálního ADS-B přijímače ve webu,
- 3 chyby lokálního receiveru -> automatický 30s backoff,
- adsb.fi/MLAT pokračuje i při vypnutém nebo nedostupném lokálním přijímači.


## 0.29.1-github-ready-cz-buildfix

- Oprava kompilace veřejné CZ verze: obnovena funkce `startupStatus()` v `src/main.cpp`.
- Funkční logika `0.29.0-github-ready-cz` zůstává beze změny.

## 0.29.0-github-ready-cz

První veřejná česká větev bez osobních výchozích údajů:

- HOME latitude/longitude v NVS a webu,
- Open-Meteo jako plnohodnotný základ bez účtu,
- Weather Underground volitelně,
- lokální ADS-B volitelně,
- adsb.fi pro celou ČR,
- LightningMaps plain JSON,
- 10km bleskový alarm,
- stabilní OTA s blackoutem displeje,
- česká mapa a CET/CEST.

## Vývojová řada 0.28.x

Významné milníky:

- `0.28.0-altitude-calibration` – kalibrace barometrické výšky,
- `0.28.5` až `0.28.9` – stabilizace web OTA a restartu,
- `0.28.10` – nová ikona blesku,
- `0.28.12` – blesky nezávislé na radarové animaci,
- `0.28.14/15` – přechod na LightningMaps plain JSON,
- `0.28.16–0.28.20` – hybridní lokální ADS-B + adsb.fi, PSRAM/buffered HTTP úpravy.

Podrobné změny jsou v kořenovém `CHANGELOG.md` repozitáře.
