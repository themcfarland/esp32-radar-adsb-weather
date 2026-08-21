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

Ve webu lze uložit až 5 Wi-Fi profilů. Po uložení se zařízení restartuje a připojí k první dostupné povolené síti.

## Failsafe Wi-Fi

Pokud nefunguje žádný z až pěti povolených Wi-Fi profilů, firmware po neúspěšném připojení znovu aktivuje konfigurační AP. Za běhu se Wi-Fi zkouší obnovovat každých přibližně 15 s a profily se střídají; pokud se reconnect nepodaří, AP zůstane dostupné pro opravu nastavení.

## OTA

Po prvním USB uploadu lze další verze nahrávat z webového nastavení. OTA používá dvě aplikační partitions. Při zápisu se zobrazí jednoduchá OTA obrazovka, podsvícení se před samotným zápisem vypne a po úspěchu se zařízení automaticky restartuje.
