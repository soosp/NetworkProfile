# Changelog

All notable changes to this project will be documented in this file.

The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- Fix includes in Quick Start example

### Added

- Expose a previously protected, configuration validator function
  to the public API for all configuration types:
  - `static bool isValidConfig(const NetworkConfig& cfg);`
  - `static bool isValidConfig(const WiFiConfig& cfg);`
- Document the new API additions.

## [0.1.0] - 2026-07-16

First public release.

### Added

- Thread-safe configuration model for network interfaces, with a common
  `NetworkProfile` base and `WiFiProfile` / `EthProfile` specialisations.
- DHCP or static addressing (IP, netmask, gateway, DNS), interface priority,
  device hostname and MAC address.
- Optional NTP server configuration, compile-time sized via
  `NETWORK_PROFILE_NTP_SERVER_COUNT` (default 0 on AVR to save RAM); all NTP
  fields and code compile away cleanly when disabled.
- WiFi-specific fields: SSID, password and TX power.
- Persistent storage through the platform's Preferences layer (native on ESP32,
  the external Preferences library on ESP8266, and EEPROMPreferences on AVR),
  with a namespaced `saveCfg()` / `loadCfg()` API.
- Consistent snapshot reads/writes under a single mutex acquisition, so a
  concurrent reader never sees a half-updated configuration.
- API reference (`API.md`).

[Unreleased]: https://github.com/soosp/NetworkProfile/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/soosp/NetworkProfile/releases/tag/v0.1.0
