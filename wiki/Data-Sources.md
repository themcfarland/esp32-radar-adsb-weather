# Datové zdroje

## ČHMÚ

Firmware stahuje radarový kompozit MAX_Z masked jako PNG. Používá posledních šest dostupných pětiminutových snímků.

Výchozí obnova:

```text
5 minut
```

## ADS-B

Firmware očekává kompatibilní JSON s polem `aircraft`, typicky z dump1090, readsb nebo tar1090:

```text
http://IP_ADRESA:PORT/data/aircraft.json
```

Výchozí obnova:

```text
2 sekundy
```

## Weather Underground PWS

Aktuální hodnoty stanice:

- teplota,
- vlhkost,
- vítr a nárazy,
- tlak,
- intenzita srážek,
- souřadnice stanice.

WU API klíč je povinný pro aktuální PWS data.

## Open-Meteo

Open-Meteo je hlavní zdroj forecastu a nevyžaduje API klíč. Firmware vybírá časové body:

```text
+3 h, +6 h, +9 h, +12 h, +24 h, +48 h
```

## WU/TWC forecast

Pokud Open-Meteo selže a WU klíč je dostupný, firmware zkusí produkty `2day` a `3day`.

## Astronomie

Slunce a Měsíc se počítají lokálně z času a souřadnic stanice. Síť se používá jen pro synchronizaci času přes NTP.
