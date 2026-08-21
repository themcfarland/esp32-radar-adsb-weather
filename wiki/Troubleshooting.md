# Troubleshooting

## Nevidím konfigurační AP

Očekávané SSID:

```text
Radar-ADSB-Setup-XXXX
```

Heslo:

```text
radarsetup
```

Pokud je zařízení připojeno k domácí Wi-Fi, AP se automaticky vypíná. Při ztrátě Wi-Fi se po neúspěšném reconnectu znovu aktivuje.

## Uložil jsem špatné heslo Wi-Fi

Po restartu zařízení zkusí připojení. Když selže, vrátí se do konfiguračního AP. Připojte se na `192.168.4.1` a údaje opravte.

## `ADSB local: HTTP -11`

`-11` odpovídá timeoutu při čtení. Ověřte URL lokálního `aircraft.json` v browseru a LAN dostupnost přijímače. Internetová adsb.fi vrstva může fungovat i bez lokálního receiveru.

## adsb.fi HTTP 200, ale JSON je neúplný

Firmware používá PSRAM body buffer a až poté JSON parser. Pokud Serial ukazuje timeout při těle HTTP, jde o síťový/TLS přenos, nikoli o neplatnou API URL.

## `setSocketOption(): Bad file number`

Tato hláška se může objevit po uzavření síťového socketu v Arduino-ESP32. Pokud následující request a funkce normálně pokračují, nejde sama o sobě o fatální chybu.

## LightningMaps je připojeno, ale nejsou blesky

Feed může legitimně vracet prázdné `strokes`. Diagnostika rozlišuje živý JSON stream od samotné přítomnosti blesků. Při zastavení platných rámců watchdog spojení obnoví.

## Blesky vypadají jako svislé čáry

Aktuální větev používá plain JSON LightningMaps a blesky kreslí nezávisle na radarových snímcích. Čerstvý zásah je blesk, starší stopa je menší bod/kříž. Pokud se problém vrátí, porovnejte `lat/lon/id` ze Serial logu se zdrojem LightningMaps.

## OTA se nahraje, ale displej během zápisu vypadá špatně

Aktuální OTA před zápisem vypíná podsvícení a do LVGL během flashování nesahá. Používejte firmware z aktuální větve 0.29.x nebo novější.

## BMP180 nenalezen

Zkontrolujte 3V3/GND, SDA GPIO8, SCL GPIO9 a čip ID `0x55`. Nepoužívejte 5 V, pokud modul není pro 5 V jednoznačně určen.
