# Kontrola verze 0.7.1-audited

Datum kontroly: 2026-08-04
Cilovy hardware: Waveshare ESP32-S3-Touch-LCD-7, 800 x 480, ST7262, GT911, CH422G.

## Overeno offline

- PlatformIO profil pouziva ESP32-S3, 8 MB OPI PSRAM a 8 MB flash.
- Hlavni a testovaci prostredi maji oddelene `setup()` a `loop()`.
- Arduino-ESP32 3.0.7 je pripnuto pres pioarduino 51.03.07.
- Verze LVGL a knihoven panelu jsou pripnute na vzajemne kompatibilni rady.
- Behem behu se nevola zmena PCLK ani restart RGB panelu.
- Pred sestavenim se ovladac upravi na 14 MHz PCLK a 20radkovy bounce buffer.
- Pokud patch nelze aplikovat, build se zamerne zastavi chybou.
- Velke mapove, radarove a JSON buffery se alokuji v PSRAM.
- Radarove PNG se pri animaci znovu nedekoduji; pouziva se sestibodova RGB332 cache.
- Bezna petiminutova aktualizace stahuje pouze jeden novy radarovy snimek.
- Opraven je parser 39znakovych nazvu souboru CHMI.
- ADS-B URL je `http://192.168.1.170:8080/data/aircraft.json`.
- Predpoved pouziva Weather.com a pri chybe Open-Meteo.
- Staticky audit, kontrola zavorek a test patchovaciho skriptu prosly.
- ZIP archiv byl nasledne otestovan prikazem `unzip -t`.

## Co nelze overit bez uzivatelova zarizeni

- Dostupnost lokalniho ADS-B serveru z Wi-Fi site displeje.
- Platnost konkretniho Weather Underground API klice.
- Fyzicka stabilita konkretni revize LCD po nekolikahodinovem provozu.
- Uplny PlatformIO build nebyl v pracovnim kontejneru spusten, protoze zde neni
  PlatformIO toolchain ani pristup k jeho instalaci. Predchozi verze projektu ale
  na uzivatelove pocitaci prosla kompilaci a byla spustena na skutecnem displeji.

## Co musi byt videt pri sestaveni

V logu buildu musi byt oba radky:

```text
[display-patch] RGB bounce buffer = 20 lines: patched ...
[display-patch] RGB PCLK = 14 MHz: patched ...
```

nebo pri dalsim buildu varianta `already configured`. Pokud se patch nenajde,
build v teto verzi skonci chybou a vadny firmware se nevytvori.
