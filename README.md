# NetworkProfile

Thread-safe Arduino library for network interface configuration management.

## Features

- **WiFiProfile** and **EthProfile** classes for WiFi and Ethernet interfaces
- DHCP or static IP configuration (IP, subnet mask, gateway, DNS)
- Up to 3 NTP servers per profile (IP or FQDN, validated via the
  [Host](https://github.com/soosp/Host) library)
- Hostname with automatic MAC-based fallback (e.g. `ESP-02D6FC`)
- MAC address management — hardware default and user override
- Interface priority for fallback scenarios (e.g. ETH → WiFi)
- Atomic bulk configuration via `NetworkConfig` / `WiFiConfig` structs
- JSON serialisation (`toJson()`)
- Persistent storage via ESP32/ESP8266 `Preferences` library
  (`saveCfg()` / `loadCfg()` / `clearCfg()`)
- Persistent storage via AVR `EEPROMPreferences` library
  (`saveCfg()` / `loadCfg()` / `clearCfg()`)
- Thread-safe on ESP32 via `std::timed_mutex`; single-context platforms
  (ESP8266, AVR) need no lock
- **Compile-time memory tuning** — buffer sizes and server counts configurable
  via preprocessor macros (see [Memory Optimisation](#memory-optimisation))

## Dependencies

- [`Host`](https://github.com/soosp/Host) — network host representation
  (IPv4 or FQDN)
- [`vshymanskyy/Preferences`](https://github.com/vshymanskyy/Preferences) —
  **ESP8266 only**; provides the same `Preferences` API as ESP32. On ESP32 this
  is part of the Arduino core and requires no separate installation.
- [`soosp/EEPROMPreferences`](https://github.com/soosp/EEPROMPreferences) —
  **AVR only**; provides `Preferences` compatible API backed by EEPROM.

## Platform Support

|Platform|Thread safety|Persistence|MAC generation|
|--|--|--|--|
|ESP32|`std::timed_mutex`|`Preferences` (built-in)|`esp_read_mac()` per interface|
|ESP8266|Cooperative (no lock)|`Preferences` (external lib)|`WiFi.macAddress()`|
|ATmega328PB/48PB/88PB/168PB|—|`EEPROMPreferences` (external lib)|Signature bytes (unique)|
|Other AVR|—|`EEPROMPreferences` (external lib)|`NETWORK_PROFILE_DEFAULT_ETH_MAC`|

## Installation

### PlatformIO

PlatformIO resolves all dependencies automatically, including the
ESP8266-specific `Preferences` and AVR specific `EEPROMPreferences` libraries:

```ini
lib_deps =
    soosp/NetworkProfile
```

### Arduino Library Manager

Search for **NetworkProfile** in the Arduino IDE Library Manager.

> **ESP8266 note:** The Arduino Library Manager cannot express
> platform-specific dependencies. If you are using this library on ESP8266,
> install the `Preferences` library by Volodymyr Shymanskyy separately
> (search for it in the Library Manager). For AVR platform install
> `EEPROMPreferences` by Péter Soós.

## Quick Start

```cpp
#include <WiFiProfile.h>
#include <EthProfile.h>

WiFiProfile wifiProfile;
EthProfile  ethProfile;

void setup() {
    // Configure WiFi profile
    WiFiProfile::WiFiConfig cfg;
    cfg.dhcp     = true;
    cfg.priority = 1;
    strncpy(cfg.ssid,     "MyNetwork",    sizeof(cfg.ssid)   - 1);
    strncpy(cfg.password, "MyPassword",   sizeof(cfg.password) - 1);
    strncpy(cfg.ntp[0],   "pool.ntp.org", sizeof(cfg.ntp[0]) - 1);
    wifiProfile.setConfig(cfg);

    // Save to flash
    wifiProfile.saveCfg("net_wifi");

    // JSON output
    char json[NetworkProfile::JSON_SIZE];
    wifiProfile.toJson(json, sizeof(json));
    Serial.println(json);
}
```

> **On `MAX_SSID_LEN`:** the examples copy into the fixed-size buffers with
> `sizeof(dest) - 1`, which is safe regardless of how the length limits are
> exposed. Note that `MAX_SSID_LEN` is a **macro**, not a class constant: the
> ESP32 WiFi SDK already defines `MAX_SSID_LEN` as a macro (= 32), so a class
> constant of the same name would collide with it. It is kept as a guarded
> macro for a uniform API across ESP32 and ESP8266, and is therefore used
> unqualified (`MAX_SSID_LEN`, never `WiFiProfile::MAX_SSID_LEN`).
> `MAX_PASSWORD_LEN` has no such collision and remains a class constant
> (`WiFiProfile::MAX_PASSWORD_LEN`).

## Configuration Constants

For the full list of compile-time macros and class constants, see
[API.md](API.md).

The most commonly needed macros for memory optimisation:

|Macro|Default|Description|
|--|--:|--|
|`NETWORK_PROFILE_DNS_SERVER_COUNT`|[See Library defaults](#library-defaults)|DNS server slots.|
|`NETWORK_PROFILE_NTP_SERVER_COUNT`|[See Library defaults](#library-defaults)|NTP server slots. Set to 0 to disable NTP entirely.|
|`NETWORK_PROFILE_HOSTNAME_LEN`|63|Maximum hostname length.|
|`HOST_FQDN_LEN`|253|Maximum NTP-server string (IP/FQDN) length.|
|`NETWORK_PROFILE_DEFAULT_ETH_MAC`|—|Required on AVR without unique serial number.|

### Library defaults

The defaults below can be configured via `NETWORK_PROFILE_DNS_SERVER_COUNT` and
`NETWORK_PROFILE_NTP_SERVER_COUNT` macros.

|Platform|DNS (default)|DNS (max)|NTP (default)|NTP (max)|Notes|
|--|--:|--:|--:|--:|--|
|AVR|1|1|0|1|`DNSClient` limits the number of DNS servers to 1. NTP is disabled by default.|
|ESP8266|2|2|3|3||
|ESP32|2|3|3|3|LwIP supports 3 DNS servers, but only 2 can be configured via WiFi API so the default lowered to 2.|

## Memory Optimisation

On memory-constrained platforms (e.g. AVR), the default configuration reserves
significantly more RAM than most applications require. All buffer sizes are
controlled by compile-time macros and can be reduced without any code changes.

### Where RAM goes (default configuration)

The dominant cost per `NetworkProfile` instance comes from the NTP server array
and the hostname buffers:

|Field|Formula|Default size|
|--|--|--|
|`_ntp[NTP_SERVER_COUNT]`|`NTP_SERVER_COUNT × (HOST_FQDN_LEN + 1)`|3 × 254 = **762 bytes**|
|`_hostname`|`NETWORK_PROFILE_HOSTNAME_LEN + 1`|**64 bytes**|
|`_hostnameDefault`|`NETWORK_PROFILE_HOSTNAME_LEN + 1`|**64 bytes**|
|`NetworkConfig::ntp[][]` (stack)|`NTP_SERVER_COUNT × (HOST_FQDN_LEN + 1)`|3 × 254 = **762 bytes**|

### Lite configuration example

The following settings are sufficient for a single NTP server identified by a
bare IP address or a short hostname:

```cpp
// platformio.ini
[env:avr_lite]
build_flags =
    -DHOST_FQDN_LEN=15
    -DHOST_FQDN_LABEL_LEN=15
    -DNETWORK_PROFILE_NTP_SERVER_COUNT=1
    -DNETWORK_PROFILE_DNS_SERVER_COUNT=1
    -DNETWORK_PROFILE_HOSTNAME_LEN=15
```

Or equivalently, defined before any library include:

```cpp
#define HOST_FQDN_LEN                    15   // fits "255.255.255.255"
#define HOST_FQDN_LABEL_LEN              15
#define NETWORK_PROFILE_NTP_SERVER_COUNT  1
#define NETWORK_PROFILE_DNS_SERVER_COUNT  1
#define NETWORK_PROFILE_HOSTNAME_LEN     15
```

### RAM savings (per profile instance)

|Field|Default|Lite|Saving|
|--|--|--|--|
|`_dns[]`|2 × 4 = 8 B|1 × 4 = 4 B|**4 B**|
|`_ntp[]`|3 × 254 = 762 B|1 × 16 = 16 B|**746 B**|
|`_hostname`|64 B|16 B|**48 B**|
|`_hostnameDefault`|64 B|16 B|**48 B**|
|**Total per instance**|||**≈ 846 B**|

> **Note:** `NetworkConfig` (used in `setConfig()` / `getConfig()`) contains
its own `dns[][]` and `ntp[][]` array of the same dimensions. When allocated
on the stack, it also benefits from the reduced `HOST_FQDN_LEN` and
`NETWORK_PROFILE_NTP_SERVER_COUNT` values.

### Constraints

- `HOST_FQDN_LEN` and `HOST_FQDN_LABEL_LEN` must satisfy
  `HOST_FQDN_LEN >= HOST_FQDN_LABEL_LEN`; the compiler will error if not.
- `NETWORK_PROFILE_HOSTNAME_LEN` minimum is 10 (needed for the generated
  `ESP-XXYYZZ` / `AVR-XXYYZZ` default hostname).
- `NETWORK_PROFILE_DNS_SERVER_COUNT` can be set to 1 instead of the default 2.
- `NETWORK_PROFILE_NTP_SERVER_COUNT` can be set to 0 to completely eliminate
  the NTP array.
- All macros must be defined identically in every translation unit that
  includes the library headers. The safest place is `platformio.ini`
  `build_flags` or an early `#define` in the project's global config header.

## Design Notes

- **NetworkProfile** is a pure data/persistence class — it does not call
  `WiFi.begin()`, `ETH.begin()`, or any hardware API (except the persistent
  storage). Applying the profile to the hardware is the responsibility of the
  [NetworkManager](https://github.com/soosp/NetworkManager) library or your
  custom solution.
- **Thread safety**: on ESP32 all getters and setters acquire a
  `std::timed_mutex`. On single-context platforms — ESP8266 (cooperatively
  scheduled, no preemption) and AVR (single-threaded) — no lock is needed and
  `_lock()`/`_unlock()` compile to no-ops. NTP servers are stored as plain
  strings and guarded by the same `NetworkProfile` mutex as every other field.
- **MAC address**: `getMac()` and `getHostname()` support a `ConfigSource`
  parameter to select between the active (user-set) value and the hardware
  default.
- **No heap allocation**: all buffers are fixed-size. The `String` type is not
  used anywhere in the library.
- **DHCP-provided NTP servers**: if a profile has no NTP server configured
  (`isConfiguredNtp()` returns `false`),
  [NetworkManager](https://github.com/soosp/NetworkManager) requests the NTP
  server address via DHCP option 42 instead of falling back to a hardcoded
  default. This is handled entirely on the adapter side, since the request must
  be enabled *before* the DHCP handshake completes — see the NetworkManager
  documentation for details.
- **Persistence is transactional**: `saveCfg()` writes a completion marker
  last, so `loadCfg()` returns `false` for a namespace that was never saved
  *or* whose save was interrupted (e.g. power loss) — it never loads a
  half-written profile. The idiomatic first-run pattern is therefore:

  ```cpp
  if (!profile.loadCfg("eth")) {
      // nothing valid stored yet — apply defaults and persist them
      profile.setConfig(defaults);
      profile.saveCfg("eth");
  }
  ```

## License

MIT
