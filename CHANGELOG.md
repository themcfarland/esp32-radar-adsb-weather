# Changelog

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
