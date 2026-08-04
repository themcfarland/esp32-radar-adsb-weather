# Hardware

## Podporovaná deska

Firmware je určen pro původní Waveshare ESP32-S3-Touch-LCD-7:

- ESP32-S3,
- LCD 800 × 480,
- řadič ST7262,
- kapacitní dotyk GT911,
- IO expander CH422G,
- 8 MB flash,
- 8 MB OPI PSRAM.

Varianty 7B a 7C mohou používat jiný hardware nebo ovladač a nejsou podporované bez úpravy projektu.

## Napájení a USB

Při nahrávání použijte kvalitní datový USB kabel. Při nestabilním startu nebo starém posunutém obrazu proveďte úplné vypnutí a opětovné zapnutí napájení.

## Test displeje

PlatformIO prostředí:

```text
waveshare-display-test
```

ověří:

- detekovanou velikost flash a PSRAM,
- rozlišení 800 × 480,
- základní vykreslení,
- dotyk GT911.
