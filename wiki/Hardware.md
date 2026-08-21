# Hardware

## Podporovaná deska

- Waveshare ESP32-S3-Touch-LCD-7,
- 800×480,
- LCD ST7262,
- dotyk GT911,
- IO expander CH422G,
- 8 MB flash,
- 8 MB OPI PSRAM.

Projekt je laděn pro původní variantu. Verze 7B/7C nemusí být bez změny display driveru kompatibilní.

## BMP180

Podporovaný barometr je BMP180 na adrese `0x77`.

```text
BMP180      Waveshare
VCC/VIN     3V3
GND         GND
SDA         GPIO8
SCL         GPIO9
```

I2C0 je sdílená s GT911 a CH422G. Firmware proto nepouští samostatné `Wire.begin()` na stejné piny, ale pracuje s již inicializovaným ESP-IDF I2C0.

Při správné detekci se v Serial Monitoru objeví:

```text
Barometer: BMP180 detected on shared ESP-IDF I2C0 at 0x77, chip ID 0x55
```

## Lokální ADS-B přijímač

Není povinný. Může to být například readsb/dump1090 na Raspberry Pi/Orange Pi nebo jiném zařízení v LAN. Firmware potřebuje URL vedoucí na `aircraft.json`.
