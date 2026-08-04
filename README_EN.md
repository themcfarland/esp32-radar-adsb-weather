# ESP32 Czech radar + ADS-B + weather

Firmware for the original **Waveshare ESP32-S3-Touch-LCD-7** board with an 800 × 480 ST7262 RGB display and GT911 capacitive touch controller.

Current version: `0.16.0-touch-map-zoom-nvs`

## Features

- animated Czech Hydrometeorological Institute radar composite,
- 300-point Czech Republic outline based on geoBoundaries,
- local ADS-B traffic from dump1090/readsb/tar1090,
- current Weather Underground PWS observations,
- forecasts for +3, +6, +9, +12, +24 and +48 hours,
- Open-Meteo primary forecast with WU/TWC fallback,
- local Sun and Moon calculations,
- touch controls for pause, refresh and map zoom,
- map sequence: full Czech Republic → 50 km → 25 km → 10 km → full map,
- persistent map center and zoom mode stored in ESP32 NVS,
- PSRAM double buffering and compact radar cache.

## Hardware

This repository targets the original Waveshare ESP32-S3-Touch-LCD-7:

- 800 × 480 ST7262 RGB panel,
- GT911 touch,
- CH422G IO expander,
- 8 MB flash,
- 8 MB OPI PSRAM.

It is not a drop-in firmware for the 7B or 7C revisions.

## Setup

```bash
git clone https://github.com/OK5TVR/esp32-radar-adsb-weather.git
cd esp32-radar-adsb-weather
cp include/secrets.example.h include/secrets.h
```

Edit `include/secrets.h`, then build with PlatformIO:

```bash
pio run -t clean
pio run -e waveshare-esp32-s3-touch-lcd-7
pio run -e waveshare-esp32-s3-touch-lcd-7 -t upload
pio device monitor -b 115200
```

The file `include/secrets.h` is excluded by `.gitignore` and must never be committed.

Detailed Czech documentation is available in [README.md](README.md) and the [`wiki`](wiki/) folder.

## License

No source-code license has been selected yet. Add a `LICENSE` file before broad reuse or redistribution.

## Author

**Tomáš Vlas, OK5TVR**
