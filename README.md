# NetworkProfile

Thread-safe Arduino library for network interface configuration management.

## Features

- **WiFiProfile** and **EthProfile** classes for WiFi and Ethernet interfaces
- **MqttProfile** for the broker settings a device needs to reach an MQTT server,
  including an optional Home Assistant discovery flag
  (address, port, TLS, credentials) — storage and validation only, with no MQTT
  client of its own
- DHCP or static IP configuration (IP, subnet mask, gateway, DNS)
- **SyslogProfile** for the syslog server a device sends its log to (address,
  port, severity floor, facility) — storage and validation only, with no
  sender of its own
- **NtpProfile** for the NTP servers a device synchronises from — one set for the
  device rather than a copy in every interface, and more than one instance where
  interfaces really do reach different servers (IP or FQDN, validated via the
  [Host](https://github.com/soosp/Host) library)
- Hostname with automatic MAC-based fallback (e.g. `ESP-02D6FC`)
- MAC address management — hardware default and user override
- Interface priority for fallback scenarios (e.g. ETH → WiFi)
- WiFi transmit power, validated against what the radio can actually
  produce (see [API.md](API.md#tx-power))
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
|`NTP_PROFILE_ENABLED`|1, or 0 on AVR|Whether NTP exists in the build at all.|
|`NTP_PROFILE_SERVER_COUNT`|[See Library defaults](#library-defaults)|NTP server slots, 1 to 3. A count, not a switch — use `NTP_PROFILE_ENABLED=0` to leave NTP out.|
|`NETWORK_PROFILE_HOSTNAME_LEN`|63|Maximum hostname length.|
|`HOST_FQDN_LEN`|253|Maximum host string (IP/FQDN) length — NTP servers, MQTT broker.|
|`NETWORK_PROFILE_DEFAULT_ETH_MAC`|—|Required on AVR without unique serial number.|

### Library defaults

The defaults below can be configured via `NETWORK_PROFILE_DNS_SERVER_COUNT` and
`NTP_PROFILE_SERVER_COUNT` macros.

|Platform|DNS (default)|DNS (max)|NTP|NTP servers (default)|NTP servers (max)|Notes|
|--|--:|--:|:-:|--:|--:|--|
|AVR|1|1|**off**|1|1|`DNSClient` limits the number of DNS servers to 1. NTP is opt-in: `NTP_PROFILE_ENABLED` also guards NetworkManager's UDP SNTP client here.|
|ESP8266|2|2|on|1|3||
|ESP32|2|3|on|3|3|LwIP supports 3 DNS servers, but only 2 can be configured via WiFi API so the default lowered to 2.|

## Memory Optimisation

On memory-constrained platforms (e.g. AVR), the default configuration reserves
significantly more RAM than most applications require. All buffer sizes are
controlled by compile-time macros and can be reduced without any code changes.

### Where RAM goes (default configuration)

What a `NetworkProfile` costs is now mostly the hostname buffers — the NTP
servers moved out to `NtpProfile`, of which a device declares one rather than one
per interface:

|Field|Formula|Default size|
|--|--|--|
|`_hostname`|`NETWORK_PROFILE_HOSTNAME_LEN + 1`|**64 bytes**|
|`_hostnameDefault`|`NETWORK_PROFILE_HOSTNAME_LEN + 1`|**64 bytes**|
|`_dns[]`|`DNS_SERVER_COUNT × 4`|**8 bytes**|

`NtpProfile` is charged per instance, and one instance is the usual case — the
servers used to be a copy inside every interface's profile:

|Field|Formula|Default size|
|--|--|--|
|`NtpConfig::server[]`|`NtpProfile::SERVER_COUNT × (HOST_FQDN_LEN + 1)`|3 × 254 = **762 bytes**|

A device whose interfaces reach different time servers declares one per
interface and pays for each, which is the same arithmetic as before the split —
the difference is that it is now a choice rather than the only option.

Declaring no `NtpProfile` costs nothing at all: no instance, no buffers, and the
linker drops the code.

### Lite configuration example

The following settings are sufficient for a single NTP server identified by a
bare IP address or a short hostname:

```cpp
// platformio.ini
[env:avr_lite]
build_flags =
    -DHOST_FQDN_LEN=15
    -DHOST_FQDN_LABEL_LEN=15
    -DNETWORK_PROFILE_DNS_SERVER_COUNT=1
    -DNETWORK_PROFILE_HOSTNAME_LEN=15
```

Or equivalently, defined before any library include:

```cpp
#define HOST_FQDN_LEN                    15   // fits "255.255.255.255"
#define HOST_FQDN_LABEL_LEN              15
#define NETWORK_PROFILE_DNS_SERVER_COUNT  1
#define NETWORK_PROFILE_HOSTNAME_LEN     15
```

### RAM savings

Per `NetworkProfile` instance — so multiplied by the number of interfaces:

|Field|Default|Lite|Saving|
|--|--|--|--|
|`_dns[]`|2 × 4 = 8 B|1 × 4 = 4 B|**4 B**|
|`_hostname`|64 B|16 B|**48 B**|
|`_hostnameDefault`|64 B|16 B|**48 B**|
|**Total per instance**|||**≈ 100 B**|

Once for the device, in `NtpProfile`:

|Field|Default|Lite|Saving|
|--|--|--|--|
|`server[]`|3 × 254 = 762 B|1 × 16 = 16 B|**746 B**|

> **Note:** `NetworkConfig` and `NtpConfig` (used by `setConfig()` /
`getConfig()`) hold arrays of the same dimensions. Allocated on the stack, they
benefit from the same reductions — which is why `HOST_FQDN_LEN` is the single
most effective knob here: it is multiplied by every profile and every NTP slot.

### Constraints

- `HOST_FQDN_LEN` and `HOST_FQDN_LABEL_LEN` must satisfy
  `HOST_FQDN_LEN >= HOST_FQDN_LABEL_LEN`; the compiler will error if not.
- `NETWORK_PROFILE_HOSTNAME_LEN` minimum is 10 (needed for the generated
  `ESP-XXYYZZ` / `AVR-XXYYZZ` default hostname).
- `NETWORK_PROFILE_DNS_SERVER_COUNT` can be set to 1 instead of the default 2.
- `NTP_PROFILE_ENABLED` set to 0 leaves NTP out of the build entirely — the
  profile class and NetworkManager's SNTP support alike. Simply not declaring a
  profile already costs nothing, so this is worth setting only to drop the
  manager's side too; on AVR, where that side is a UDP client with its own
  socket, it is the default.
- `NTP_PROFILE_SERVER_COUNT` says how many servers a profile holds, 1 to 3. It is
  a count, not a switch: zero is refused.
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
  `_lock()`/`_unlock()` compile to no-ops. Host strings are stored as plain
  strings and guarded by the same `NetworkProfile` mutex as every other field.
- **MAC address**: `getMac()` and `getHostname()` support a `ConfigSource`
  parameter to select between the active (user-set) value and the hardware
  default.
- **No heap allocation**: all buffers are fixed-size. The `String` type is not
  used anywhere in the library.
- **TX power is a grid, and on ESP32 a set**: `setTxPower()` refuses a value
  that is not a multiple of 0.25 dBm rather than snapping to the nearest, on
  both platforms. On ESP32 it goes further and accepts only the dozen discrete
  levels the radio supports — the IDF would take anything and quietly step
  down, which leaves a device transmitting at a power nobody chose and nothing
  to show for it. On ESP8266 the whole 0.25 dBm grid within range is available.
- **DHCP-provided NTP servers**: if no `NtpProfile` holds a server,
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

### More than one NtpProfile

The usual case is one: a device has one clock, and the same servers are reachable
whichever way it is connected. But interfaces sometimes genuinely differ — an
internal time server on a wired LAN that a phone hotspot cannot reach, say — and
then the answer is a second instance rather than a field per interface:

```cpp
NtpProfile ethNtp, wifiNtp;      // one per interface

ethAdapter.setNtpProfile(ethNtp);      // in setup(), before begin()
wifiAdapter.setNtpProfile(wifiNtp);
```

Bound to the interface rather than swapped on the manager as the serving
interface changes. The manager has to know whether an interface should take
DHCP's time server (option 42) *before* it starts that interface, since lwIP
settles the question during the handshake — and at that moment no event has told
the application which interface is taking over. Only the adapter carries the
answer that early.

An adapter without a profile of its own falls back to the device-level one, so
the two mix: bind only the interface that differs. `NetworkManager`'s
FallbackTestRunner example gives its two interfaces different servers so the log
shows which one the SNTP client actually ended up using.

Two objects say "these interfaces see different time servers" plainly. The field
buried in each interface's configuration said it by accident, and mostly said it
twice with the same value.

### Why MQTT settings live here

The library is named after network interfaces, and an MQTT broker is not one. It
is here for the same reason the NTP servers are: both are addresses a networked
device stores in order to reach a service, both are edited on the same page and
backed up in the same file, and a device that needs one almost always needs the
other. Splitting them into separate libraries would mean three dependencies and
three release cycles for things that are always deployed together.

The boundary that does hold is a different one: `MqttProfile` stores and
validates, and contains no MQTT client, no topics and no connection logic. The
same profile therefore serves an ESP32 task, an ESP8266 loop and an AVR with no
portal at all — the application owns the client, this owns what survives a
reboot.

`haDiscovery` sits on the storing side of that line: a flag saying whether the
application should publish Home Assistant discovery messages. The profile
publishes nothing and knows nothing about Home Assistant beyond the name. It is
a base field rather than something for a subclass because it has no natural
siblings — a second discovery convention would turn it into an enum rather than
add a second boolean — and because a derived field would be invisible to
`MqttConfigComponent`, which works through the base config type. See
[API.md](API.md#why-hadiscovery-is-a-base-field).

## License

MIT
