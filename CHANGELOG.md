# Changelog

All notable changes to this project will be documented in this file.

The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `json_cat_esc()` in `detail/json_tools.h` — appends a string escaped, without
  a scratch buffer. A fully escaped host name needs six bytes per character, and
  a temporary that size has no business on an ESP8266 stack, least of all inside
  an async request handler. `MqttProfile::toJson()` uses it.

### Changed

- To optimize memory usage, the default setting on the ESP8266 is one NTP server
  per profile. The maximum number remains three, as supported by LwIP. The
  default can be overridden using the `NETWORK_PROFILE_NTP_SERVER_COUNT` macro.
  
## [0.6.0] - 2026-08-05

### Added

- `MqttProfile` — stored MQTT broker settings (enabled, host, port, TLS,
  username, password) with `checkConfig()`, `saveCfg()`/`loadCfg()` and
  `toJson()`. It lives here rather than in a library of its own for the same
  reason the NTP servers do: it is something a networked device stores in order
  to reach a service, and the two are always deployed together. It knows nothing
  about MQTT itself — no client, no topics, no connection — so the same profile
  serves an ESP32 task, an ESP8266 loop and an AVR without a portal.
  Topics are deliberately absent, being project-specific; the virtual hooks are
  there for a subclass that needs them.
- `enabled` is a field of its own rather than an empty host standing in for it:
  "switched off" and "not configured yet" are different facts, and a device taken
  off the broker for a while should not have to forget its address to do it.
- `MqttProfile` locks the same way `NetworkProfile` does — a `std::timed_mutex`
  where preemption exists (ESP32), a no-op on AVR and ESP8266 — because the
  settings are copied field by field: the portal saving while a client task
  reads would otherwise hand out half of each. `getConfig()` and `setConfig()`
  return `bool` accordingly. `checkConfig()` takes the config as a parameter, as
  `NetworkProfile`'s does, so validation runs on a staged copy and needs no lock
  at all — but it is virtual rather than static, since a static one called
  through a base reference silently skips a subclass's own rules.
- `detail/prefs_tools.h` with `prefs_putStr()` — the empty-string storage rule
  (store non-empty, remove the key otherwise) now has one implementation instead
  of one per profile class.

### Changed

- Passing a subclass's config to a base-class `checkConfig()`, `isValidConfig()`
  or `setConfig()` is now a compile error rather than a silent half-check. These
  are static, so the compiler picks by declared type: a `WiFiConfig` handed to
  the base version — which is what happens the moment a caller holds a
  `NetworkProfile&` — checked the addressing, skipped the SSID, password and
  TX-power rules, and reported Ok. Deleted derived-type overloads now catch it.

  Where it fires, either call through the concrete type
  (`WiFiProfile::checkConfig(cfg)`), which is nearly always what was meant, or
  state that only the addressing is of interest:
  `checkConfig(static_cast<const NetworkConfig&>(cfg))`. Slicing a config into a
  `NetworkConfig` variable first still loses the subclass fields — by then they
  are gone, and no signature can catch that. `MqttProfile` carries the same
  guard.

  Impact on the dependent libraries was one call site, inside `WiFiProfile`
  itself; `NetworkManager`, `AsyncConfigPortal` and `AsyncWiFiPortal` all already
  called through the concrete type or cast explicitly.

### Note

- The MQTT password is not written to `toJson()`, matching `WiFiProfile`: a
  backup file may travels through mail and cloud storage. Restoring one
  therefore needs the broker password entered again.

## [0.5.3] - 2026-08-05

### Changed

- The JSON string escaper moved to `detail/json_tools.h` as `json_escape()`, so
  `NetworkManager` can use it too — a Wi-Fi SSID is arbitrary bytes, and it is
  now escaped by one implementation whether it comes from the stored profile or
  from the live link. `_jsonEscapeStr()` stays as a thin wrapper.

## [0.5.2] - 2026-08-05

### Fixed

- JSON serialisation no longer crashes on ESP8266 and AVR. `toJson()` passed
  `PSTR()` literals to `_strncatJson()` and `snprintf()`, which read their source
  from RAM: on a platform where program space is a separate address space, that
  reads flash byte by byte and faults (exception 3 / `LoadStoreError` on
  ESP8266). The `PSTR()` format strings now go to `snprintf_P()`, and program-space
  fragments to a new `_strncatJson_P()`. ESP32 was unaffected, its flash being
  memory-mapped.
- `WiFiProfile::_doJson()` had the same defect in the `ssid` and `txpwr` fields.
- `toJson()` compared `snprintf()`'s `int` result against a `size_t` buffer size,
  which warns under `-Wsign-compare` and would convert a negative return (an
  encoding error) into a huge unsigned value. The check now goes through
  `_fitted()`, which treats a negative result and an overlong one as separate,
  explicit cases.
- `WiFiProfile::_doJson()` did not check for truncation at all: an over-long SSID
  field was silently cut instead of reporting failure.

### Added

- `_strncatJson_P()` — the program-space counterpart of `_strncatJson()`. The two
  are kept separate on purpose: a `const char*` does not say which address space
  it points into, so the call site, not the helper, states where the source
  lives. Mixing them is what the fix above was about.
- `_fitted()` — reports whether an `snprintf()`-family call wrote its whole
  string, keeping the negative-return and truncation cases apart.
- `detail/json_tools.h` — internal header holding the JSON append and truncation
  helpers, so `NetworkManager` can share them instead of keeping a second copy.
  It offers both append behaviours under distinct names, because the two builders
  genuinely differ: `json_cat()` appends what fits and reports truncation, while
  `json_catAtomic()` writes nothing when the text would not fit. Each has a `_P`
  variant for program-space sources. `NetworkProfile`'s own helpers stay as thin
  wrappers, so subclasses and existing call sites are unaffected.

## [0.5.1] - 2026-07-31

### Added

- `ipToStr()` — public helper that serialises an `IPAddress` to `"a.b.c.d"`, or
  to an empty string when it is `0.0.0.0` (unset), and reports truncation.

### Fixed

- `toJson()` now serialises an unset optional address (a gateway or DNS server
  left at `0.0.0.0`) as an empty string rather than `"0.0.0.0"`, so a field the
  user cleared round-trips to the form blank instead of reappearing as
  `"0.0.0.0"` after a reboot.

## [0.5.0] - 2026-07-30

### Added

- `checkConfig()` on `NetworkProfile` and `WiFiProfile` — validates a whole
  config and returns *which* check failed as a `ConfigCheck`, rather than a bare
  `bool`. `NetworkProfile::checkConfig()` covers the hostname, the static
  addressing block (ip / mask / gateway / DNS coherence, checked only under
  static addressing) and the NTP entries; `WiFiProfile::checkConfig()` adds SSID,
  password and TX power. `isValidConfig()` is now a thin `bool` wrapper over it.
- `ConfigCheck` enum naming the specific validation result.

### Changed

- **Breaking (behaviour):** `setConfig()` / `isValidConfig()` now reject a static
  config that is not internally coherent — a host ip that is unusable
  (`0.0.0.0/8`, loopback, multicast or reserved) or is the subnet network or
  broadcast address; a non-contiguous or empty mask; a gateway off the configured
  subnet; or a DNS/NTP address in one of those unusable ranges. A gateway or DNS
  of `0.0.0.0` means "not set" and is accepted, so an isolated segment with no
  default route is valid. Configs previously stored with these mistakes are now
  refused.
- If output has truncated anywhere `toJson()` outputs an empty buffer instead
  of a malformed JSON stucture.
- refactor: Replace bitwise AND (`&=`) with logical AND (`&&`) to avoid implicit
  bool-to-int conversions and clarify boolean logic intent.

## [0.4.1] - 2026-07-24

### Fixed

- Add missing `#include <WiFi.h>` to `WiFiProfile.h` for ESP32. It is needed
  for resolving `WIFI_POWER_*` macros.

## [0.4.0] - 2026-07-24

### Changed

- `isValidTxPower()` now tests membership of the levels the radio actually
  supports on ESP32, instead of accepting any multiple of
  `WIFI_TX_POWER_STEP_dBm`. **Breaking:** values such as 19.25 dBm are now
  rejected by `setTxPower()` and `setConfig()`; previously they were stored and
  then silently stepped down to a lower level by the IDF, leaving the radio at a
  power nobody had chosen. ESP8266 is unchanged — there the 0.25 dBm grid is
  genuine.

### Added

- `WIFI_TX_POWER_LEVELS_Q[]` and `WIFI_TX_POWER_LEVEL_COUNT` (ESP32) — the
  supported transmit power levels in quarter-dBm, derived from the Arduino
  core's `wifi_power_t` enum so a core update carries them along.
- `#error` for architectures other than ESP32 and ESP8266, replacing a silent
  fall-through that surfaced as unrelated errors far from the real cause.
- Add architecture guard to `WiFiProfile.h`.

## [0.3.0] - 2026-07-23

### Added

- Add WiFi security type enum and getter.

## [0.2.0] - 2026-07-23

### Fixed

- Fix includes in Quick Start example
- Fix Changelog links

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

[Unreleased]: https://github.com/soosp/NetworkProfile/compare/0.6.0...HEAD
[0.6.0]: https://github.com/soosp/NetworkProfile/compare/0.5.3...0.6.0
[0.5.3]: https://github.com/soosp/NetworkProfile/compare/0.5.2...0.5.3
[0.5.2]: https://github.com/soosp/NetworkProfile/compare/0.5.1...0.5.2
[0.5.1]: https://github.com/soosp/NetworkProfile/compare/0.5.0...0.5.1
[0.5.0]: https://github.com/soosp/NetworkProfile/compare/0.4.1...0.5.0
[0.4.1]: https://github.com/soosp/NetworkProfile/compare/0.4.0...0.4.1
[0.4.0]: https://github.com/soosp/NetworkProfile/compare/0.3.0...0.4.0
[0.3.0]: https://github.com/soosp/NetworkProfile/compare/0.2.0...0.3.0
[0.2.0]: https://github.com/soosp/NetworkProfile/compare/0.1.0...0.2.0
[0.1.0]: https://github.com/soosp/NetworkProfile/releases/tag/0.1.0
