# Changelog

All notable changes to this project will be documented in this file.

The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `MqttProfile::MqttConfig` gains `haDiscovery`, a flag for whether the
  application should publish Home Assistant MQTT discovery messages. Topics
  remain absent by design: those are project-specific addresses, while this is a
  yes/no about a broker-side convention. A base field rather than a subclass's
  because it has no natural siblings — a second convention would make it an enum
  rather than add a second boolean — and because a derived field is invisible to
  `MqttConfigComponent`, which works through `MqttConfig`.

## [0.7.0] - 2026-08-26

### Fixed

- `loadCfg()` could restore a value the last save did not contain. `saveCfg()`
  *removes* the key of an empty string rather than storing a blank, and
  `Preferences::getString()`/`getBytes()` leave the destination untouched when a
  key is absent — so clearing the hostname, SSID, password, MAC or MQTT broker
  address, saving, and then loading back into the same object handed the old
  value straight back, and the store read as agreeing with it. The affected
  fields are now reset to their defaults before they are read, in
  `NetworkProfile`, `WiFiProfile` and `MqttProfile` alike. The numeric fields
  were never affected: they pass a default to the getter, which does the same
  job. Neither was a load into a freshly constructed profile, which is the usual
  boot path — this only ever bit a reload into a populated object, such as a
  configuration page reverting its edits.

  `MqttProfile::_doLoad()` additionally passed the *current* value as each
  getter's default, which had the same effect on the bools and the port; it now
  passes a default-constructed `MqttConfig`'s.
- `WiFiProfile(const WiFiConfig&)` dropped `cfg.txPower`. The constructor applied
  the SSID and the password but left the transmit power at the platform default,
  while `setConfig(WiFiConfig)` applied all three — so the same struct produced
  two different profiles depending on whether it arrived through the constructor
  or the setter. It is now validated and stored like the other two, and ignored
  the same way when invalid, since a constructor cannot report a rejection.
- `WiFiProfile::_doLoad()` could load a TX power of NAN.
  `Preferences::getFloat()` answers a missing key with NAN unless given a
  default, and the call passed none — so a namespace written before `txpwr`
  existed came back with a poisoned `_txPower`, after which every
  `isValidTxPower()` comparison against it was false and the radio never got a
  usable value. It now falls back to `DEFAULT_WIFI_TX_POWER_dBm`, and does the
  same for a stored value outside what this build accepts: the range is not the
  same on both platforms (ESP32 discrete levels up to 19.5 dBm, ESP8266
  continuous up to 20.5), so a namespace carried between them, or a build whose
  `WIFI_PROFILE_DEFAULT_WIFI_TX_POWER` changed, can hold one the radio would
  refuse. The rest of the profile still loads and `loadCfg()` still returns
  `true`; only the TX power is replaced.

### Added

- `NtpProfile::clearCfg()` and `MqttProfile::clearCfg()`, matching
  `NetworkProfile`'s. Both classes could write a namespace and read it back but
  not erase it, which left a factory reset unable to finish the job: a device
  wiped of its network configuration still came back holding the time servers it
  synchronised from and — worse — the broker's address and the credentials for
  it. `MqttProfile` leaves the password out of `toJson()` precisely because a
  backup file travels, so leaving it in flash through a reset was the same
  concern answered two different ways.

  Same contract as `NetworkProfile::clearCfg()`, including that it clears the
  store rather than the object: the instance keeps what it holds until something
  loads over it.
- `NetworkProfile::configCheckName()`, naming a `ConfigCheck` value for logs and
  configuration UIs. `checkConfig()` exists so a caller can report *which* field
  was refused rather than only that something was, but with no name for the
  value every caller had to write the same nine-case switch — so in practice
  callers reported nothing, or skipped the check. The string is flash-resident,
  costing no RAM on AVR, and names the field rather than the remedy: a UI knows
  better than this library how to phrase advice to its user.
- `NtpProfile` — the NTP servers as a device-level setting, with `getServer()` /
  `setServer()`, `checkConfig()`, `saveCfg()` / `loadCfg()` and `toJson()`. A
  device has one clock, so the servers it synchronises from belong to the device,
  not to whichever interface is up: keeping a copy in every `NetworkProfile` meant
  the same `pool.ntp.org` typed twice, stored twice, and carried twice in the
  largest document the portal serves. Where interfaces really do reach different
  time servers, two `NtpProfile` instances say so plainly.
- `NtpProfile::importLegacy()` reads servers written by an earlier version, which
  stored them per interface under `ntp0`..`ntpN` in the network profile's own
  namespace. Without it an upgraded device would come back with no time servers
  and no indication why. **Scheduled for removal in 1.0.0.**
- `detail/ntp_config.h` holds `NTP_PROFILE_SERVER_COUNT` on its own, because two
  callers ask different things of it: `NtpProfile.h` asks how many servers it
  holds and refuses to be included when the answer is none, while NetworkManager
  asks whether to compile its NTP support and must be able to ask that when the
  answer is no.
- `NTP_PROFILE_ENABLED` decides whether NTP exists in the build;
  `NTP_PROFILE_SERVER_COUNT` says how many servers a profile holds, and is now a
  count in its own right (1..3) rather than a switch that also happened to be a
  number. Zero no longer disables anything and is refused with a message naming
  the replacement — a profile holding no servers was never a coherent state, which
  is why the class had to error out on it. Defaults: NTP off on AVR (the switch
  also guards a UDP SNTP client there, not just a buffer), three servers on ESP32,
  one everywhere else. Code sizes buffers from `NtpProfile::SERVER_COUNT` rather
  than the macro.
- `NTP_PROFILE_SERVER_COUNT` replaces `NETWORK_PROFILE_NTP_SERVER_COUNT`; the old
  name is reported by a `#warning` rather than ignored, since a build still
  passing it would silently get the default count.
- `json_cat_esc()` in `detail/json_tools.h` — appends a string escaped, without
  a scratch buffer. A fully escaped host name needs six bytes per character, and
  a temporary that size has no business on an ESP8266 stack, least of all inside
  an async request handler. `MqttProfile::toJson()` uses it.

### Changed

- `NtpProfile::setConfig()` and `MqttProfile::setConfig()` validate what they are
  given, through their own `checkConfig()`, and store nothing unless it passes.
  Both classes already had the check written; it simply was not wired in, so the
  same object accepted through `setConfig()` what it refused through
  `setServer()`. A stored address the resolver will never accept is a clock that
  silently never syncs, or a broker never reached — the kind of failure that
  looks like a network fault.

  The three profile families now answer the same way: `setConfig()` refuses an
  invalid struct everywhere. `checkConfig()` here is virtual, so unlike
  `NetworkProfile`'s static `isValidConfig()`, a subclass's own rules apply
  through it automatically.

  What still passes is unchanged where it matters: an `NtpProfile` with every
  slot empty is a device told to take its time from DHCP, and a disabled
  `MqttProfile` keeps whatever half-filled form the user switched off. The
  portal pattern — stage a config, check it, keep it — works as before; it just
  no longer matters whether the check was remembered.

  This is a tightening. A struct that `setConfig()` used to swallow is now
  refused, and a `getConfig()` → edit → `setConfig()` round-trip of something
  loaded from an older or hand-edited namespace can fail where it did not.
  `loadCfg()` and `importLegacy()` deliberately stay permissive: the check
  belongs where a new value arrives, not where an existing one is recovered.
- `NtpProfile` and `MqttProfile` gained `configCheckName()`, matching
  `NetworkProfile`'s. Each class has its own `ConfigCheck` enum, so one shared
  function could not have covered them; without their own, a caller reporting
  *which* field was refused had to write the switch that the third class had
  been given. Now that `setConfig()` can refuse, the name is what makes the
  refusal reportable.
- `detail/prefs_tools.h` names AVR instead of catching it with `#else`, and an
  unsupported architecture now fails at the include with a message saying what a
  port would need. `EEPROMPreferences.h` declares its class unconditionally and
  guards only its `.cpp`, so the `#else` branch compiled on a fourth
  architecture and then failed at link time with `undefined reference to
  EEPROMPreferences::begin` — a long way from the cause. `NetworkProfile.h`
  already used the narrower guard; the two now agree, and the message follows
  the one `WiFiProfile.h` has carried for the same situation.
- ESP8266: `isValidTxPower()` now also requires the value to sit on the 0.25 dBm
  grid, not merely inside
  [`MIN_WIFI_TX_POWER_dBm`, `MAX_WIFI_TX_POWER_dBm`]. `setOutputPower()`
  truncates to the grid on its way to the radio, so a value between two steps
  was accepted, stored and reported back as something the radio never produced —
  the same silent step-down the ESP32 branch already existed to catch. The two
  platforms now ask the same question about the grid and differ only where they
  genuinely do: ESP32 additionally accepts just the dozen discrete levels its
  radio offers.

  `API.md` already documented ESP8266 as accepting *any multiple of 0.25 dBm*,
  so this brings the code to the documentation rather than the other way round.
  The prose above that table, which said every value in range was accepted,
  contradicted it and has been corrected.

  This narrows what is accepted. A stored TX power off the grid now fails
  validation, and `loadCfg()` replaces it with `DEFAULT_WIFI_TX_POWER_dBm` as it
  does for any other unacceptable value.
- `toJson()` prints `txpwr` with two decimals instead of `%f`'s six
  (`"txpwr":19.50`). Two are exact for the 0.25 dBm grid now enforced on both
  platforms, and the other four were always zeros. One would not do: 13.25 would
  come back as 13.2, and ESP8266 offers the whole grid, not only the halves
  ESP32's level set happens to use.

### Removed

- The library's two `using Preferences = EEPROMPreferences;` declarations, one in
  `NetworkProfile.h` and one in `detail/prefs_tools.h`. `EEPROMPreferences.h` has
  declared the alias itself since 0.2.0, so both were redundant — and the one in
  `NetworkProfile.h` was doubly so, sitting below an include of `prefs_tools.h`
  that had already brought it in.

  Redeclaring an alias to the same type is legal, which is why this went
  unnoticed, but it overrode `EEPROMPREFERENCES_NO_PREFERENCES_ALIAS` — and that
  macro is a working extension point, not just a collision escape. An application
  can define it, alias `Preferences` to its own EEPROMPreferences-compatible type
  (a different EEPROM offset, for instance), include this library afterwards, and
  the profile classes will store through the application's type. Our copies made
  the macro look available while quietly cancelling it.
- The NTP fields are gone from `NetworkProfile`: `getNtp()`, `setNtp()`,
  `isConfiguredNtp()`, the `ntp[]` array in `NetworkConfig`, `NTP_SERVER_COUNT`,
  `ConfigCheck::BadNtp`, and the `NETWORK_PROFILE_*_NTP_SERVERS` macros. Use
  `NtpProfile`; `importLegacy()` reads what an earlier version stored.

  Each profile carried its own copy — around 780 bytes for three servers — so two
  interfaces meant the same `pool.ntp.org` stored twice and free to drift. What
  is
  saved is not only the RAM: the largest document the portal serves shrinks with
  it, and there is one place to look when the time is wrong.

### Documentation

- Comments that explained the code by what it used to be are reworded to explain
  it by what it must do. A reader who never saw the earlier version cannot check
  a sentence like "would have restored the old one" against anything; the same
  point stated as a consequence — "without the reset, loading back into the same
  object would hand the old one straight back" — needs no such memory. Affects
  `_doLoad()` in `NetworkProfile`, `WiFiProfile` and `MqttProfile`, and
  `MqttProfile::setConfig()`. The development history itself belongs here, in
  this file.
- The superseded way of giving interfaces different NTP servers is corrected in
  `API.md`, `README.md` and the `NtpConfig` example. All three said to keep two
  profiles and swap them on the manager from its `CONNECTED` handler; the
  binding now lives on the adapter, because the manager settles the DHCP
  option-42 question before it starts an interface and no event has said which
  interface is taking over that early. The change was made in `NetworkManager`,
  so these were left behind.
- `README.md`'s memory table said an `NtpProfile` is charged once for the device
  rather than per interface. One instance is the usual case and the reason the
  servers moved out of the network profiles, but per-interface instances are now
  a supported choice and cost accordingly.
- `NtpProfile::getServer()` said it returns false for a short buffer. It
  truncates with `snprintf()` and reports true, which is what the accessor above
  it in `NetworkManager` already documented. The doxygen now says so, and says
  why it matters: a truncated host name is a different server, not a partial
  answer.
- `API.md` still listed NTP among the fields `NetworkProfile::setConfig()` and
  `getConfig()` carry, and claimed NTP entries were validated there. They moved
  to `NtpProfile`.
- `library.properties` described the library as WiFiProfile and EthProfile with
  NTP among their fields. It names all four classes now.
- Comments and documents left behind by the NTP move and by earlier reworks are
  corrected. `NetworkConfig`'s note described its address fields as strings
  validated as IP-or-FQDN, which was true only while they held NTP servers; the
  default constructor's summary read "the hostname servers are empty", a sentence
  left half-edited when "and NTP" came out of it; and `JSON_LEN`'s sizing still
  accounted for three NTP servers per profile, putting the worst case at ~1150
  bytes when the real figure is now 482 (266 for an `EthProfile`). The constant
  stays at 1200 — the headroom is for fields a subclass adds through `_doJson()`,
  which are counted against the same buffer — but it now says so instead of
  implying the space is spoken for.
- The AVR persistence story is told as it is. The class header still said
  `saveCfg()`/`loadCfg()` delegate to user-supplied callbacks and `clearCfg()`
  claimed to be a no-op returning false there; all three have gone through
  EEPROMPreferences for some time, and `clear()` works. The `SaveCallback` and
  `LoadCallback` typedefs those sentences referred to are removed — they were
  declared, never used, and never called by anything.
- `NTP_PROFILE_SERVER_COUNT` is no longer described as disabling NTP when set to
  zero. It is a count (1..3); zero is refused by a `#error` naming
  `NTP_PROFILE_ENABLED`, which is the switch. `API.md` and `README.md` said
  otherwise, and the CHANGELOG carried two entries for this macro that
  contradicted each other — one describing the current behaviour, one still
  claiming zero compiles the class away. The stale half is gone, as is a third
  entry restating the ESP8266 default in terms of the obsolete
  `NETWORK_PROFILE_NTP_SERVER_COUNT` name and of NTP servers "per profile".
- `API.md` spelled `ConfigSource` as `ACTIVE`/`DEFAULT`; the enum is
  `ACTIVE`/`FACTORY`, which the surrounding prose already used. The DNS range
  was given as 1–2, but ESP32 allows 3.
- Doxygen blocks that had drifted onto the wrong declaration are moved back.
  `NetworkProfile::isValidConfig()` and `MqttProfile::checkConfig()` each had
  their description sitting immediately above the *deleted* template overload
  that follows them, so Doxygen attached it there and the real function was
  documented by nothing.
- README and API.md follow the split: the memory section now charges the servers
  once to `NtpProfile` rather than per interface, and states plainly that
  declaring no instance costs nothing. `HOST_FQDN_LEN` is called out as the most
  effective knob, being multiplied by every profile and every NTP slot.

### Examples

- `NtpConfig` rewritten for `NtpProfile`: the old one exercised `setNtp()`,
  `getNtp()` and `isConfiguredNtp()` on a `NetworkProfile`, none of which exist
  any more, so it could not compile. It now covers the class's whole surface,
  including `importLegacy()` for an upgraded device and the two-instance case for
  interfaces that really do reach different servers.
- `BasicUsage` and `BasicEthUsage` no longer write `cfg.ntp[]`, which is gone.

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

[Unreleased]: https://github.com/soosp/NetworkProfile/compare/0.7.0...HEAD
[0.7.0]: https://github.com/soosp/NetworkProfile/compare/0.6.0...0.7.0
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
