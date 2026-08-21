# OTA a diagnostika

## OTA aktualizace

1. Otevřete web zařízení.
2. V sekci OTA vyberte `firmware.bin`.
3. Web nejprve zavolá `/ota-prepare`.
4. Zařízení zobrazí jednoduchou OTA obrazovku.
5. Před zápisem se vypne podsvícení, aby RGB panel během flash operací nevykresloval poškozený obraz.
6. Firmware se zapíše do neaktivní OTA partition.
7. Po úspěšném `Update.end(true)` se zařízení restartuje.

NVS nastavení zůstává zachováno.

## Diagnostika

Webová stránka:

```text
/diagnostics
```

JSON:

```text
/api/diagnostics
```

Sledujte zejména:

- Wi-Fi stav/RSSI,
- heap free/min/largest,
- PSRAM free/min/largest,
- stav radaru,
- LightningMaps spojení a stáří dat,
- ADS-B zdroj, HTTP stav a počet letadel,
- BMP180,
- Open-Meteo/WU,
- verzi firmware.

## Paměť

Pro stabilitu TLS je důležitý nejen celkový free heap, ale hlavně `largest free block`. Pokud výrazně klesne, velké HTTPS operace mohou selhávat i při dostatku PSRAM.
