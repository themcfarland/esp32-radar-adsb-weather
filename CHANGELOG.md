# Changelog

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
