# Instalace

## Požadavky

- Waveshare **ESP32-S3-Touch-LCD-7**, původní 800×480 varianta,
- USB kabel s datovými vodiči,
- PlatformIO,
- 2.4GHz Wi-Fi,
- volitelně BMP180 a/nebo lokální ADS-B přijímač.

## PlatformIO

Projekt obsahuje environment:

```text
waveshare-esp32-s3-touch-lcd-7
```

Sestavení:

```bash
pio run -e waveshare-esp32-s3-touch-lcd-7
```

První upload přes USB:

```bash
pio run -e waveshare-esp32-s3-touch-lcd-7 -t upload
```

Serial monitor:

```bash
pio device monitor -b 115200
```

Výsledný OTA soubor je obvykle:

```text
.pio/build/waveshare-esp32-s3-touch-lcd-7/firmware.bin
```

## Volitelný test displeje

Projekt má samostatný environment:

```text
waveshare-display-test
```

Ten ověří LCD, PSRAM a dotyk bez nutnosti Wi-Fi.

## První konfigurace

Po čistém flashi nebo po factory resetu firmware nemá SSID a spustí:

```text
SSID: Radar-ADSB-Setup-XXXX
PASS: radarsetup
IP:   192.168.4.1
```

Po uložení Wi-Fi se zařízení restartuje a připojí do domácí sítě.

## Failsafe Wi-Fi

Pokud je uložené SSID/heslo chybné nebo router není dostupný, firmware po neúspěšném připojení znovu aktivuje konfigurační AP. Za běhu se Wi-Fi zkouší obnovovat každých přibližně 15 s; pokud se nepodaří reconnect, AP zůstane dostupné pro opravu nastavení.

## OTA

Po prvním USB uploadu lze další verze nahrávat z webového nastavení. OTA používá dvě aplikační partitions. Při zápisu se zobrazí jednoduchá OTA obrazovka, podsvícení se před samotným zápisem vypne a po úspěchu se zařízení automaticky restartuje.
