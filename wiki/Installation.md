# Instalace

## Požadavky

- VS Code,
- PlatformIO IDE,
- Git,
- datový USB kabel,
- původní Waveshare ESP32-S3-Touch-LCD-7.

## Klonování

```bash
git clone https://github.com/OK5TVR/esp32-radar-adsb-weather.git
cd esp32-radar-adsb-weather
```

## Konfigurační soubor

Windows PowerShell:

```powershell
Copy-Item include/secrets.example.h include/secrets.h
```

Linux/macOS:

```bash
cp include/secrets.example.h include/secrets.h
```

Vyplňte Wi-Fi, WU klíč, ID stanice a ADS-B URL.

## Čistý build

```bash
pio run -t clean
pio run -e waveshare-esp32-s3-touch-lcd-7
```

## Nahrání

```bash
pio run -e waveshare-esp32-s3-touch-lcd-7 -t upload
```

## Serial Monitor

```bash
pio device monitor -b 115200
```

## Test displeje

```bash
pio run -e waveshare-display-test
pio run -e waveshare-display-test -t upload
```
