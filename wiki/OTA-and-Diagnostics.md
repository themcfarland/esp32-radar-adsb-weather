# OTA a diagnostika

## OTA aktualizace

1. Otevřete web zařízení.
2. V sekci OTA vyberte `firmware.bin`.
3. Web nejprve zavolá `/ota-prepare`.
4. Zařízení pozastaví plánování nových síťových úloh a zobrazí jednoduchou OTA obrazovku.
5. Před zápisem se vypne podsvícení, aby RGB panel během flash operací nevykresloval poškozený obraz.
6. Firmware se zapíše do neaktivní OTA partition.
7. Po úspěšném `Update.end(true)` se zařízení restartuje.

NVS nastavení zůstává zachováno. Již běžící síťová operace se kvůli bezpečnosti socketů násilně neukončuje; nové úlohy se během OTA nespouštějí.

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

- Wi-Fi stav/RSSI a aktivní profil,
- heap free/min/largest,
- PSRAM free/min/largest,
- stav radaru,
- LightningMaps spojení a stáří dat,
- ADS-B zdroj, HTTP stav a počet letadel,
- BMP180,
- Open-Meteo/WU,
- verzi firmware.

### Síťový worker

Od verze 0.30.0 diagnostika obsahuje samostatnou kartu **Síťový worker**:

- stav workeru a případné pozastavení,
- právě aktivní úlohu,
- počet čekajících úloh,
- výsledek poslední úlohy,
- dobu poslední a nejdelší úlohy,
- počet dokončených úloh,
- počet chyb a přeskočení kvůli backoffu.

Pomalý nebo nedostupný server se tak dá diagnostikovat bez toho, aby musel blokovat samotné UI.

## Paměť

Pro stabilitu TLS je důležitý nejen celkový free heap, ale hlavně `largest free block`. Pokud výrazně klesne, velké HTTPS operace mohou selhávat i při dostatku PSRAM. Síťový worker záměrně serializuje velké TLS operace, aby se tomuto stavu předcházelo.

Diagnostika v0.30.9 navíc rozlišuje TLS paměť `OK / VAROVANI / KRITICKA RAM`.
Hodnota kolem 35 kB největšího bloku sama o sobě již nezastavuje HTTPS; recovery
LightningMaps se spouští až po skutečném transportním selhání, případně při
opravdu kritickém pre-flight stavu.
