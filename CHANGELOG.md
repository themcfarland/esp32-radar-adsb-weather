## 0.28.12 - Lightning live independent overlay

- Decoupled Blitzortung rendering completely from CHMI `radarFrame` and five-minute radar timestamps.
- Lightning is now drawn like ADS-B: the same realtime lat/lon overlay remains visible while historical radar images animate underneath.
- Trail colour uses actual current strike age only: 0-2 min white, 2-5 min yellow, 5-10 min orange, 10-20 min red.
- New strikes request an immediate map redraw; a 30 s age refresh keeps colours/expiry correct even with radar animation paused.
- Radar animation no longer runs merely because the lightning layer is enabled.
- Web diagnostics now show the number of buffered Blitzortung strikes instead of fictitious lightning frame counts.
- 10 km proximity alert, smooth bolt icon and proven OTA blackout/reboot behavior are unchanged.

## 0.28.11 - Lightning frame sync + realtime latest frame

- Blitzortung historical strikes are rendered only in their exact five-minute CHMI radar slot instead of being repeated for a 20-minute window in every frame.
- This removes the stationary vertical accumulation visible when a thunderstorm moves between radar frames.
- The newest radar frame also accepts Blitzortung strikes newer than the latest CHMI timestamp for up to five minutes, so new lightning appears immediately.
- Historical colour bands remain referenced to the newest CHMI frame; live-overlay colours use actual current time.
- OTA blackout/reboot behavior and the smooth bolt icon remain unchanged.

## 0.28.10-lightning-bolt-icon

- Replaced the sparse pixel lightning bitmap with a continuous filled zig-zag bolt made from two triangles.
- Added a subtle one-pixel dark shadow so white/yellow strikes remain readable over bright radar echoes.
- Lightning trail age colours and all stable 0.28.9 OTA behaviour are unchanged.

## 0.28.9-ota-reboot-blackout

- Fixed successful OTA getting stuck after `OTA: complete`: the upload callback no longer performs any success-side LVGL refresh.
- A successful `Update.end(true)` immediately arms a fallback reboot, even if the browser never reaches the normal POST result handler.
- The normal success response shortens the reboot delay to one second and logs the armed restart.
- Added OTA display blackout: the preflight screen is shown first, then the physical backlight is switched off before flash writes begin. This hides ESP32-S3 RGB/PSRAM DMA shifts without touching LVGL or restarting the RGB panel during the upload.
- OTA failures restore the backlight and repaint/resynchronise the panel only after the synchronous upload handler has returned.

## 0.28.8-ota-preflight

- OTA display rendering is moved out of the multipart `/update` upload callback.
- Browser first calls `/ota-prepare`, waits 700 ms, then starts the firmware upload.
- Removed `lv_refr_now()` from OTA start because it can block on RGB-panel flush/DMA completion.
- Added serial checkpoints before/after `Update.begin()`, on the first upload chunk, and every 256 kB.
- Added a 15 s preflight timeout so an abandoned OTA preparation returns to the dashboard.

## 0.28.7-ota-stable

- Fixed unreliable browser OTA transfers on the ESP32-S3 RGB display build.
- The browser now sends the exact `.bin` byte size in the `/update?size=` query and `Update.begin()` uses that size, matching Espressif's current OTAWebUpdater pattern.
- Added an explicit final byte-count check before `Update.end(true)`; a truncated upload is aborted instead of being accepted.
- Removed LVGL redraws and `esp_lcd_rgb_panel_restart()` calls from the flash-write callback. The OTA screen is drawn once before writing and remains static until success/failure.
- Added `Connection: close` on the successful OTA response and clearer serial logging for expected/written byte counts.
- Lightning trail, realtime 10 km warning and all 0.28.6 functionality are retained.

## 0.28.6-lightning-trail-ota

- Added a 20-minute Blitzortung lightning activity trail synchronized to each displayed CHMI radar frame.
- Lightning age colours: 0-2 min white, 2-5 min yellow, 5-10 min orange, 10-20 min red; strikes older than 20 minutes are hidden.
- Older strikes are rendered first and fresh strikes last so the newest/brighter activity stays visible when symbols overlap.
- Existing realtime 10 km proximity warning and stable full-screen OTA update view are retained.

## 0.28.5-ota-screen

- Added a dedicated minimal full-screen OTA view on the 7-inch LCD.
- The OTA screen is rendered before `Update.begin()` and shows only firmware upload state, bytes written and a do-not-power-off warning.
- Radar, Blitzortung, weather, map redraws and settings work remain paused for the complete OTA POST transaction.
- RGB panel DMA is resynchronised approximately every 64 KiB / 400 ms of flash writes and again after OTA finalisation, greatly reducing the scrambled-display effect during browser OTA.
- Successful OTA shows a restart message until reboot; failed OTA shows the error briefly, restores the dashboard and performs one final LCD resync.
- Existing realtime Blitzortung lightning, 10 km lightning proximity warning and OTA partition safety are retained.

## 0.28.4-lightning-proximity-alert-ota

- Added realtime lightning proximity warning around the home/station position.
- A Blitzortung strike from the last 10 minutes inside a true 10 km great-circle radius activates the warning.
- Draws a red geographic 10 km outline around the home marker; the geometry stays correct in full Czech Republic and 50/25/10 km zoom modes.
- The warning is independent of the currently animated five-minute radar frame and clears automatically when the last nearby strike becomes older than 10 minutes.
- Existing Blitzortung WSS/LZW stream, synchronized radar animation and browser OTA are retained.

## 0.28.3-blitzortung-realtime-ota

- Replaced EUMETSAT PNG/WMS lightning overlays with realtime Blitzortung WebSocket strikes.
- Added WSS fallback rotation `ws7 -> ws1 -> ws8`, subscription `{"a":111}`, heartbeat and reconnect handling.
- Added ESP32 LZW decoder compatible with the browser stream; only the message header (`time`, `lat`, `lon`) is decoded, avoiding the large `sig` station array.
- Stores up to 4096 nearby strikes in PSRAM and groups them into the same six five-minute slots as the CHMI radar animation.
- Draws individual yellow/white lightning symbols below borders/cities/ADS-B, including 50/25/10 km zoom views.
- Browser OTA from 0.28.2 is retained unchanged.

## 0.28.2-lightning-animation-ota

- Added six-frame EUMETSAT MTG-LI AFA animation synchronized to the six CHMI radar frame timestamps.
- AFA WMS `time=` requests are shifted by one five-minute step because EUMETView timestamps each 5-minute accumulation by interval start while CHMI radar filenames represent interval end.
- Runtime lightning refresh reuses matching PSRAM overlays and normally downloads only one new frame every five minutes.
- Lightning animation now follows the same frame index and pause state as the radar animation.
- Added browser-based OTA firmware upload using the existing dual OTA app partitions. Successful uploads reboot into the new firmware and preserve NVS settings.

## 0.28.1 - EUMETSAT MTG-LI lightning overlay

### Added
- Optional EUMETSAT MTG Lightning Imager `mtg_fd:li_afa` WMS layer over the Czech map.
- Five-minute lightning refresh with PNG validation and atomic replacement of the last valid overlay.
- Compact RGB332 lightning overlay in PSRAM, including Web Mercator resampling for 50/25/10 km map zooms.
- Independent lightning layer switch in web settings with NVS persistence.
- Lightning source, readiness and update age in web diagnostics and JSON APIs.

### Preserved
- CHMI radar remains below the lightning layer; borders, cities and ADS-B aircraft stay above it.
- Existing BMP180 altitude calibration, Zambretti, LCD timing and RAM-only radar protections remain unchanged.

## 0.28.0 - Altitude calibration helper

### Added
- Web calibration helper that derives sensor altitude from the current raw BMP180 pressure, a reference sea-level pressure and the WU outdoor temperature used for pressure reduction.
- Button for loading the current WU sea-level pressure into the calibration form.
- Configured altitude, MSL fine correction and WU reference pressure in diagnostics and `/api/diagnostics`.

### Changed
- Renamed the pressure offset field to clarify that it is a fine MSL correction applied after altitude reduction.
- The calibration helper resets the fine correction to zero so altitude remains the primary physical correction.
- The three-hour tendency continues to use unreduced station pressure and is not affected by altitude calibration.

# Changelog

## 0.27.0 - BMP180 on shared display I2C

### Fixed
- BMP180 detection now reuses the ESP-IDF I2C0 bus already initialized by the Waveshare display, GT911 touch and CH422G expander.
- Removed the previous Arduino `Wire` probe, which was never initialized by the display library and therefore returned no sensor.
- Added BMP180 chip-ID, calibration and first-measurement validation with four startup retries.
- Added clear serial and web diagnostic messages for no ACK, wrong chip ID and invalid calibration.

### Changed
- The barometer implementation is now dedicated to the confirmed BMP180 at address `0x77`.
- BMP180 temperature and pressure compensation is performed locally from the Bosch factory calibration registers.
- Touch polling is paused with the LVGL mutex during each BMP180 transaction sequence to avoid concurrent access to the shared bus.
- Removed unused Adafruit BMP085/BMP280/BME280 dependencies.

### Preserved
- WU outdoor-temperature reduction, 24-hour pressure graph and Zambretti forecast remain unchanged.
- The firmware does not call `Wire.begin()` and does not reinstall or reconfigure the display I2C controller.

## 0.26.0 - Startup screen

### Added
- Full-screen startup presentation after LCD initialization.
- Animated eight-dot loading indicator and percentage progress bar.
- One-line live status for LCD, Wi-Fi/AP, UI buffers, I2C barometer, radar, weather, astronomy and ADS-B initialization.
- Firmware version and `Vytvoril OK5TVR` credit on the startup screen.
- Final green `OK` state before switching to the main dashboard.

### Changed
- The main LVGL screen is built off-screen while the startup screen remains visible.
- The weekly backlight schedule is applied after the startup presentation, so boot progress remains visible.

## 0.25.0 - WU outdoor temperature pressure reduction

### Changed
- Sea-level pressure conversion now uses a RAM-only rolling average of outdoor Weather Underground temperature over up to 12 hours.
- Duplicate WU observations are ignored by observation epoch.
- The three-hour tendency is calculated from unreduced station pressure, preventing outdoor-temperature changes from creating a false trend.
- The pressure graph and Zambretti continue to use sea-level pressure.
- Standard 15 C is used when WU temperature is unavailable or older than 12 hours; indoor barometer temperature is diagnostic only.

### Added
- Reduction temperature, source, WU average, sample count, averaging span and observation age in web diagnostics and JSON API.

## 0.24.0 - Zambretti local forecast

### Added
- Classic Zambretti forecast with A-Z condition codes and Czech display texts.
- Seasonal Northern-Hemisphere correction based on the local NTP month.
- Optional 16-point wind-direction correction using current Weather Underground wind direction.
- Zambretti code, trend class, wind use and seasonal correction in diagnostics and JSON API.
- Host-side reference tests for representative Zambretti cases.

### Changed
- Replaced the previous threshold-only local weather text with Zambretti output.
- Sea-level pressure conversion now includes measured sensor temperature.
- The display waits for an almost complete three-hour pressure window before publishing a local forecast.
- Five-minute history point timestamps remain fixed, preventing compression of the regression time axis by one-minute sensor refreshes.
- The pressure panel title and summary now identify the Zambretti result explicitly.

### Notes
- Internet cards remain numerical forecasts for +3, +6 and +9 hours.
- Zambretti is a categorical local short-range forecast; the dashed pressure line remains a trend projection, not a numerical Zambretti pressure forecast.
- Wind correction is omitted automatically when no valid WU wind direction is available.

## 0.23.0 - I2C barometer and pressure trend

### Added
- Automatic detection of BMP180, BMP280 and BME280 sensors at I2C addresses 0x76/0x77.
- Sea-level pressure conversion using configurable sensor altitude and pressure offset.
- One-minute pressure sampling and a RAM-only 24-hour history with five-minute graph points.
- Three-hour linear-regression pressure trend, three-hour delta and local short-term weather indication.
- Dashed pressure projection for +3, +6 and +9 hours.
- Barometer configuration on the main web page and live barometer diagnostics.

### Changed
- Internet forecast cards are reduced to +3, +6 and +9 hours.
- The freed sidebar area is used for a 24-hour pressure chart and local pressure forecast.
- Forecast data requests are reduced to the first 12 hours.

### Safety
- Pressure history is held only in RAM and creates no periodic flash writes.
- Trend projection remains hidden until at least 45 minutes of valid observations are available.
- The local pressure forecast is explicitly documented as indicative, not an official forecast.

## 0.22.0 - Weekly backlight schedule

### Added
- Prominent diagnostics link on the main configuration page.
- Independent Monday-to-Sunday backlight start and end times.
- Per-day schedule enable switch and global weekly schedule switch.
- Overnight schedule support, for example 18:00-02:00.
- Touch wake for 60 seconds while outside the active backlight interval.
- Invisible wake overlay so the first wake touch does not activate map controls.
- Backlight state, schedule state and wake countdown in web diagnostics and JSON API.

### Safety
- The backlight remains on until NTP time is synchronized.
- Only the CH422G backlight output is switched; network services and data updates continue.
- Existing conservative LCD timing, 20-line bounce buffer and manual RGB DMA recovery are unchanged.

## 0.21.0 - Diagnostics and local clock

### Added
- Web diagnostics page at `/diagnostics` with live five-second refresh.
- Diagnostics JSON API at `/api/diagnostics`.
- Local clock and date in the display header.
- NTP synchronization status, CET/CEST timezone, memory, network, data-source and LCD statistics.

### Changed
- The static `RADAR CR + ADS-B` header title is replaced by local time and date.
- Header refresh interval is one second.


## 0.20.2 - Display patch compatibility fix

### Fixed
- The PlatformIO display-driver patch now accepts any previous numeric bounce-buffer value, including 30 left by v0.20.0 in `.pio/libdeps`.
- A missing or changed driver macro is reported as a warning instead of aborting the complete build.
- Added a fallback search for the bounce-buffer macro if the library changes the header filename.

## 0.20.1 - Conservative LCD recovery

### Fixed
- Removed the five-second periodic RGB DMA restart that worsened horizontal image movement on tested hardware.
- Restored the proven 20-line RGB bounce buffer and original redraw pacing from v0.19.0.

### Added
- Manual `Srovnat LCD` action remains available in the web interface.

## 0.19.0 - Home web, three aircraft alerts and layer switches

### Added

- Persistent web configuration on the home Wi-Fi IP and mDNS hostname.
- Three independent aircraft highlight targets.
- Current-aircraft selector with assignment to alert slots 1, 2 or 3.
- Independent radar and ADS-B layer switches.
- Layer and three-alert state in `/api/status`.
- Runtime application of display settings without restart.
- Automatic migration of the v0.18.0 single alert into slot 1.

### Changed

- Wi-Fi restart is now required only after SSID or password changes.
- Map footer reports the active layer combination.
- Radar legend is hidden when the radar layer is disabled.
- Highlighted aircraft use a distinct ring colour for each of the three slots.
- NVS writes from web settings request one RGB DMA recovery on the next VSYNC.

### Preserved

- First-run AP and captive portal.
- RAM-only runtime radar updates.
- Touch map zoom and persistent viewport.
- Open-Meteo forecast fallback.

## 0.18.0 - Web configuration and aircraft highlight

- First-run Wi-Fi AP and captive portal.
- Home-network web interface.
- One visual-only aircraft highlight.
