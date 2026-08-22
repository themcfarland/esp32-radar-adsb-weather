# Mapa a dotyk

## Rozložení

LCD má 800×480 px. Mapa zabírá přibližně 600×444 px a pravý informační panel 200 px.

Pevný český výřez:

```text
lon 11.70 ... 19.00
lat 48.30 ... 51.30
```


## Horní lišta

Od verze **0.29.6** je horní lišta pouze informační. Tlačítka **PAUZA** a **OBNOVIT** byla z LCD odstraněna; radarová animace i periodické aktualizace dat běží automaticky. Uvolněný prostor využívá delší stavový text.

## Vrstvy

Pořadí je zjednodušeně:

1. základní mapa,
2. radar ČHMÚ,
3. LightningMaps,
4. hranice/města/HOME,
5. ADS-B letadla a popisky,
6. UI.

## Zoom

Dotykem lze přepínat:

- celá ČR,
- 50 km,
- 25 km,
- 10 km.

Lokální zoomy se centrovají kolem HOME. Poslední výřez lze uložit do NVS.

## Blesky

Čerstvý zásah je kreslen jako malý blesk, starší stopa menšími barevnými body/kříži. To omezuje falešný dojem svislých linií při husté bouřkové aktivitě.

## ADS-B

Letadla jsou kreslena podle latitude/longitude a tracku. MLAT lze v popisku odlišit. Duplicitní ICAO z lokálního receiveru a adsb.fi se sloučí s prioritou lokálního zdroje.

## Časové údaje

Od verze **0.29.5** se čas radarového snímku zobrazuje v místním českém čase CET/CEST. ČHMÚ data jsou nadále stahována podle UTC názvů souborů; převod se provádí pouze při vykreslení na displej. Tím je zachována správná časová posloupnost dat i automatický přechod mezi zimním a letním časem.
