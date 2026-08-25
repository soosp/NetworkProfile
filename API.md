# NetworkProfile — API Reference

## Table of Contents

- [Compile-time macros](#compile-time-macros)
- [NetworkProfile](#networkprofile-class)
  - [Constants](#constants)
  - [Types and enums](#types-and-enums)
  - [NetworkConfig struct](#networkconfig-struct)
  - [Constructors](#constructors)
  - [Interface type](#interface-type)
  - [IP configuration](#ip-configuration)
  - [Hostname](#hostname)
  - [NTP servers](#ntp-servers)
  - [Priority](#priority)
  - [MAC address](#mac-address)
  - [Bulk configuration](#bulk-configuration)
  - [Serialisation](#serialisation)
  - [Persistence](#persistence)
  - [Static utilities](#static-utilities)
- [WiFiProfile](#wifiprofile-class)
  - [Constants](#wifiprofile-constants)
  - [WiFiSecurity enum](#wifisecurity-enum)
  - [WiFiCredentials struct](#wificredentials-struct)
  - [WiFiConfig struct](#wificonfig-struct)
  - [Constructors](#wifiprofile-constructors)
  - [Security type getter](#security-type-getter)
  - [Credentials](#credentials)
  - [TX power](#tx-power)
  - [Bulk configuration](#wifiprofile-bulk-configuration)
  - [Static validators](#static-validators)
- [EthProfile](#ethprofile-class)
  - [Constructors](#ethprofile-constructors)
- [NtpProfile](#ntpprofile-class)
- [MqttProfile](#mqttprofile-class)
  - [Constants](#mqttprofile-constants)
  - [MqttConfig struct](#mqttconfig-struct)
  - [ConfigCheck enum](#mqttprofile-configcheck-enum)
  - [Configuration](#mqttprofile-configuration)
  - [Validation](#mqttprofile-validation)
  - [Persistence](#mqttprofile-persistence)
  - [Serialisation](#mqttprofile-serialisation)
  - [Extending it](#extending-mqttprofile)
- [Passing a subclass config to a base function](#passing-a-subclass-config-to-a-base-function)

---

## Compile-time macros

Define these **before** including any library header, or via `platformio.ini` `build_flags`.

|Macro|Default|Range|Description|
|---|---|---|---|
|`HOST_FQDN_LEN`|253|1 – 253|Maximum FQDN length in `Host` objects.|
|`HOST_FQDN_LABEL_LEN`|63|1 – 63|Maximum single label length. Must be ≤ `HOST_FQDN_LEN`.|
|`NTP_PROFILE_ENABLED`|1, 0 on AVR|0 or 1|Whether NTP exists in the build.|
|`NTP_PROFILE_SERVER_COUNT`|3 (ESP32), 1 elsewhere|1 – 3|NTP server slots. A count, not a switch: 0 is refused with a `#error` naming `NTP_PROFILE_ENABLED`.|
|`NETWORK_PROFILE_DNS_SERVER_COUNT`|2 (1 on AVR)|1 – 3 on ESP32, 1 – 2 on ESP8266, 1 on AVR|DNS server slots per profile.|
|`NETWORK_PROFILE_HOSTNAME_LEN`|63|10 – 63|Maximum hostname length. Lower bound accommodates the generated default (e.g. `ESP-02D6FC`).|
|`NETWORK_PROFILE_JSON_LEN`|1200|—|Maximum JSON content length. The library itself needs at most 482; the rest is headroom for subclass fields added through `_doJson()`.|
|`NETWORK_PROFILE_MUTEX_TIMEOUT`|1000|—|Mutex acquisition timeout in milliseconds. Used on ESP32 only; ESP8266 (cooperative scheduling) and AVR (single-threaded) have no lock and ignore it.|
|`NETWORK_PROFILE_DEFAULT_ETH_MAC`|—|—|Required on AVR platforms without a unique serial number. Format: `"4E:52:47:30:30:31"`.|
|`WIFI_PROFILE_DEFAULT_WIFI_TX_POWER`|19.5 (ESP32) / 20.5 (ESP8266)|—|Default WiFi TX power in dBm.|

---

## NetworkProfile class

Abstract base class for all network interface profiles. Not instantiated
directly — use `WiFiProfile` or `EthProfile`.

### Constants

|Constant|Value|Description|
|---|---|---|
|`DNS_SERVER_COUNT`|2 (1 on AVR)|Effective DNS server count (reflects macro).|
|`MAC_LEN`|6|MAC address length in bytes.|
|`MAC_STR_SIZE`|18|MAC string buffer size (`"AA:BB:CC:DD:EE:FF\0"`).|
|`MUTEX_TIMEOUT`|1000|Mutex acquisition timeout in milliseconds.|
|`MAX_HOSTNAME_LEN`|63|Effective maximum hostname length (reflects macro).|
|`MAX_HOSTNAME_SIZE`|64|Hostname buffer size including null terminator.|
|`PRIORITY_INVALID`|255|Sentinel returned by `getPriority()` on mutex timeout. Valid priorities are [0, 254].|
|`JSON_LEN`|1200|Effective JSON content length (reflects macro).|
|`JSON_SIZE`|1201|JSON buffer size including null terminator.|

### Types and enums

#### `InterfaceType`

```cpp
enum class InterfaceType : uint8_t {
    UNKNOWN = 0,
    WIFI    = 1,
    ETH     = 2,
};
```

Returned by `getInterfaceType()`. Each subclass returns a compile-time constant.

#### `ConfigSource`

```cpp
enum class ConfigSource : uint8_t {
    ACTIVE  = 0,
    FACTORY = 1,
};
```

Selects between the active (user-set) value and the hardware default for
`getMac()` and `getHostname()`.

- `ACTIVE` — returns the user-set override if present; falls back to `FACTORY`
  if not set.
- `FACTORY` — always returns the generated/factory value regardless of any override.

#### `MACAddress`

```cpp
using MACAddress = uint8_t[MAC_LEN];
```

#### `ConfigCheck`

```cpp
enum class ConfigCheck : uint8_t {
    Ok = 0,
    BadHostname,
    BadIp,
    BadMask,
    GatewayOffSubnet,
    BadDns,
    BadSsid,        // WiFiProfile only
    BadPassword,    // WiFiProfile only
    BadTxPower,     // WiFiProfile only
};
```

The result of `checkConfig()`: `Ok`, or the first problem found. `BadIp` covers
a host address that is unusable (`0.0.0.0/8`, loopback, multicast or reserved)
or that is the subnet's network or broadcast address. The last three values are
produced only by `WiFiProfile::checkConfig()`.

### NetworkConfig struct

Aggregates all profile fields for atomic get/set via `setConfig()` / `getConfig()`.

```cpp
struct NetworkConfig {
    bool       dhcp     = true;
    IPAddress  ip;
    IPAddress  mask;
    IPAddress  gateway;
    IPAddress  dns[DNS_SERVER_COUNT];
    char       hostname[MAX_HOSTNAME_SIZE]                = {};
    uint8_t    priority = 0;
    MACAddress mac      = {};
};
```

**Notes:**

- `mac` sets the active MAC override. To revert to the hardware default, set
  all bytes to `0x00`.

### Constructors

```cpp
NetworkProfile();
explicit NetworkProfile(const NetworkConfig& cfg);
```

`NetworkProfile` is non-copyable. The default MAC is generated by
`_initMacDefault()`, which is called from the subclass constructor.

### Interface type

```cpp
virtual InterfaceType getInterfaceType() const = 0;
```

Returns the interface type. Compile-time constant — no mutex needed.

### IP configuration

All methods are thread-safe.

```cpp
bool      isDhcp()   const;
bool      setDhcp(bool dhcp);

IPAddress getIp()    const;
bool      setIp(IPAddress ip);

IPAddress getMask()  const;
bool      setMask(IPAddress mask);

IPAddress getGateway() const;
bool      setGateway(IPAddress gateway);

IPAddress getDns(uint8_t index = 0) const;
bool      setDns(IPAddress dns, uint8_t index = 0);
```

`getDns()` and `setDns()` accept an `index` in `[0, DNS_SERVER_COUNT)`.
Out-of-range values are clamped to 0. `getDns()` returns `0.0.0.0` on mutex
timeout.

### Hostname

```cpp
bool getHostname(char* buf, size_t len,
                 ConfigSource source = ConfigSource::ACTIVE) const;
bool setHostname(const char* hostname);
static bool isValidHostname(const char* hostname);
```

**`getHostname()`** — copies the hostname into `buf`. With
`ConfigSource::ACTIVE`, returns the user-set hostname if present, otherwise
falls back to the generated default (e.g. `ESP-02D6FC`). With
`ConfigSource::FACTORY`, always returns the generated default. Returns `false`
on truncation or mutex failure.

**`setHostname()`** — validates against RFC 1123 before storing. An empty
string resets the active hostname; subsequent `getHostname()` calls will return
the generated default.

**`isValidHostname()`** — enforces: length 1–`MAX_HOSTNAME_LEN`, ASCII
letters/digits/hyphens only, no leading or trailing hyphen, not all-numeric.

### NTP servers

Moved to `NtpProfile`. The servers a device synchronises from are a property of
the device — it has one clock — not of whichever interface happens to be up, and
a copy per profile meant the same address typed twice, stored twice, and free to
drift. See [NtpProfile](#ntpprofile-class).

`NtpProfile::importLegacy()` reads servers written by a version that still stored
them here.

### Priority

```cpp
uint8_t getPriority() const;
bool    setPriority(uint8_t priority);
```

Lower value = higher priority. Valid range: [0, 254]. Value 255 is reserved as
`PRIORITY_INVALID` and is rejected by `setPriority()`. `getPriority()` returns
`PRIORITY_INVALID` on mutex timeout.

### MAC address

```cpp
bool getMac(MACAddress mac,
            ConfigSource source = ConfigSource::ACTIVE) const;
bool setMac(const MACAddress mac);
```

**`getMac()`** — with `ConfigSource::ACTIVE`, returns the user-set override if
non-zero, otherwise the hardware default. With `ConfigSource::FACTORY`, always
returns the hardware default.

**`setMac()`** — validates with `isValidMac()` before storing. To revert to the
hardware default, use `setConfig()` with `mac` set to all zeros.

### Bulk configuration

```cpp
bool setConfig(const NetworkConfig& cfg);
bool getConfig(NetworkConfig& cfg) const;
```

**`setConfig()`** — atomically updates all base fields (IP, DNS, hostname,
priority, MAC) in a single mutex acquisition. The struct is validated before the
mutex is acquired — the profile is not modified if validation fails. NTP servers
are not among these fields; they live in `NtpProfile`.

**`getConfig()`** — atomically reads all base fields in a single
mutex acquisition. The `mac` field in the returned struct contains the active
MAC override (may be all zeros if no override is set).

### Serialisation

```cpp
bool toJson(char* json, size_t len) const;
```

Serialises the full profile to a JSON object. Password is intentionally omitted
for security.

Output fields (base class): `dhcp`, `ip`, `mask`, `gw`, `dns0`[…`dns1`],
`host`, `prio`, `mac`[, `ntp0`…`ntp2`].

`WiFiProfile` appends `ssid` and `txpwr` via `_doJson()`. `txpwr` is printed
with two decimals, which is exact for the 0.25 dBm grid `isValidTxPower()`
enforces (`"txpwr":19.50`).

Use `NetworkProfile::JSON_SIZE` as the buffer size:

```cpp
char json[NetworkProfile::JSON_SIZE];
profile.toJson(json, sizeof(json));
```

Returns `false` on mutex timeout or output truncation.

### Persistence

```cpp
bool saveCfg(const char* ns);
bool loadCfg(const char* ns);
bool clearCfg(const char* ns);
```

Use the `Preferences` namespace as a key (e.g. `"net_wifi"`, `"net_eth"`).

Persistence is **transactional**: `saveCfg()` writes a completion marker last,
so `loadCfg()` returns `false` for a namespace that was never saved *or* whose
save was interrupted (e.g. power loss) — it never loads a half-written profile.

**`saveCfg()`** — saves the full profile to non-volatile storage. Returns
`true` on success; `false` on a storage failure or mutex timeout. Because the
store is single-buffered, an interrupted re-save also invalidates the previously
stored profile (there is no rollback to the prior copy).

**`loadCfg()`** — loads the full profile from non-volatile storage. Returns
`true` on success; `false` on an empty/never-written namespace, an incomplete
save (torn write), a storage failure, or mutex timeout — so a `false` return is
the reliable "no valid saved configuration" signal used by the first-run
pattern.

`loadCfg()` resets every field to its default before reading it, so a load
restores exactly what the last save held rather than merging into whatever the
object already had. This matters because `saveCfg()` *removes* the key of an
empty string instead of storing a blank: clearing the hostname, saving, then
loading back into the same object would otherwise have handed back the old name.
Loading into a freshly constructed profile was never affected.

On `WiFiProfile`, TX power falls back to `DEFAULT_WIFI_TX_POWER_dBm` when the
stored value is missing or outside what this build accepts, rather than being
restored as-is. Two cases reach it: a namespace written before `txpwr` existed,
and a namespace carried between platforms, since the accepted range differs
(ESP32 takes discrete levels up to 19.5 dBm, ESP8266 a continuous range up to
20.5). The rest of the profile still loads and `loadCfg()` still returns `true`
— only the TX power is replaced.

**`clearCfg()`** — erases the entire Preferences namespace. After calling this,
`loadCfg()` returns `false` and a reboot will load default values.

### Static utilities

```cpp
static bool        macToStr(char* buf, size_t len, const MACAddress mac);
static bool        macFromStr(const char* str, MACAddress mac);
static bool        isValidMac(const MACAddress mac);
static bool        ipToStr(char* str, size_t len, const IPAddress ip);
static ConfigCheck checkConfig(const NetworkConfig& cfg);
static bool        isValidConfig(const NetworkConfig& cfg);
```

**`macToStr()`** — serialises a MAC address to `"AA:BB:CC:DD:EE:FF"` format
(uppercase hex, colon-separated). Buffer must be at least `MAC_STR_SIZE` bytes.

**`macFromStr()`** — parses `"AA:BB:CC:DD:EE:FF"` format (case-insensitive).

**`isValidMac()`** — returns `false` if all bytes are `0x00` (unset) or all
bytes are `0xFF` (broadcast).

**`ipToStr()`** — serialises an address to `"a.b.c.d"`, or to an empty string
when it is `0.0.0.0` (unset), so a cleared optional field (gateway, DNS)
round-trips to a form as blank rather than `"0.0.0.0"`. Returns `false` on
truncation.

**`checkConfig()`** — validates a whole config and returns *which* check failed
(`ConfigCheck`), or `Ok`. It checks the hostname, the static-addressing block
(ip / mask / gateway / DNS coherence — only under static addressing, since DHCP
leaves those fields unused) and the NTP entries. Network-free arithmetic, so it
runs before the interface is up and works on an isolated segment with no gateway.
First failure wins.

**`isValidConfig()`** — whole-config validity as a `bool`; a thin wrapper over
`checkConfig()` (`checkConfig(cfg) == ConfigCheck::Ok`). This is the gate
`setConfig()` uses, so an incoherent config can never be stored, however a UI
validated it.

---

## WiFiProfile class

Extends `NetworkProfile` with WiFi-specific credentials and TX power.

**Platform:** ESP32 and ESP8266 only. Not available on AVR.

### WiFiProfile constants

|Constant|Value|Description|
|---|---|---|
|`MAX_SSID_LEN`|32|Maximum SSID length (IEEE 802.11), excluding null terminator. Exposed as a **macro**, not a class constant — see note below.|
|`MAX_SSID_SIZE`|33|SSID buffer size including null terminator.|
|`MAX_PASSWORD_LEN`|63|Maximum WPA2-PSK password length, excluding null terminator.|
|`MAX_PASSWORD_SIZE`|64|Password buffer size including null terminator.|
|`MIN_WIFI_TX_POWER_dBm`|−1.0 (ESP32) / 0.0 (ESP8266)|Minimum TX power in dBm.|
|`MAX_WIFI_TX_POWER_dBm`|19.5 (ESP32) / 20.5 (ESP8266)|Maximum TX power in dBm.|
|`WIFI_TX_POWER_STEP_dBm`|0.25|Size of the radio's power unit in dBm. **Not** the spacing of the selectable values on ESP32 — see [TX power](#tx-power).|
|`WIFI_TX_POWER_MULTIPLIER`|4.0|Converts dBm to the platform integer unit. Used by adapters.|
|`WIFI_TX_POWER_LEVELS_Q[]`|see below|**ESP32 only.** The selectable power levels, ascending, in quarter-dBm.|
|`WIFI_TX_POWER_LEVEL_COUNT`|12 (ESP32)|**ESP32 only.** Number of entries in `WIFI_TX_POWER_LEVELS_Q`.|
|`DEFAULT_WIFI_TX_POWER_dBm`|19.5 / 20.5|Default TX power (overridable via macro).|

> **`MAX_SSID_LEN` is a macro, not a class constant.** The ESP32 WiFi SDK already
> defines `MAX_SSID_LEN` as a macro (= 32); a same-named class constant would
> collide with it. It is therefore provided as a guarded macro for a uniform API
> across ESP32 and ESP8266, and must be used unqualified (`MAX_SSID_LEN`,
> never `WiFiProfile::MAX_SSID_LEN`). `MAX_PASSWORD_LEN` has no such collision and
> remains a normal class constant (`WiFiProfile::MAX_PASSWORD_LEN`).

### `WiFiSecurity` enum

```cpp
enum class WiFiSecurity : uint8_t {
        UNKNOWN  = 0,
        OPEN     = 1,
        PASSWORD = 2,
};
```

These values are not related to actual Wi-Fi security protocols; they merely
describe the basis for authentication.

### WiFiCredentials struct

```cpp
struct WiFiCredentials {
    char ssid    [MAX_SSID_SIZE]     = {};
    char password[MAX_PASSWORD_SIZE] = {};
};
```

Used with `setWiFiCredentials()` / `getWiFiCredentials()` for atomic
SSID + password update.

### WiFiConfig struct

```cpp
struct WiFiConfig : public NetworkConfig {
    char  ssid    [MAX_SSID_SIZE]     = {};
    char  password[MAX_PASSWORD_SIZE] = {};
    float txPower                     = DEFAULT_WIFI_TX_POWER_dBm;
};
```

Extends `NetworkConfig` with WiFi credentials and TX power. Used with
`setConfig(WiFiConfig)` / `getConfig(WiFiConfig)` /
`isValidConfig(WiFiConfig&)`.

### WiFiProfile constructors

```cpp
WiFiProfile();
explicit WiFiProfile(const NetworkConfig& cfg);
explicit WiFiProfile(const WiFiConfig& cfg);
```

When constructing from `WiFiConfig`, an invalid SSID, password or TX power is
silently ignored: the SSID stays empty, the password stays empty (= open
network) and the TX power stays at `DEFAULT_WIFI_TX_POWER_dBm`. Base IP
configuration is always applied.

This is deliberately more forgiving than `setConfig(WiFiConfig)`, which rejects
the whole struct and returns `false` if any field fails validation — a
constructor has no way to report one. Where a rejection matters, construct
empty and use `setConfig()`, or run `checkConfig()` on the struct first.

### Security type getter

```cpp
WiFiSecurity getSecurity() const;
```

As an empty string is accepted as password and treated as an open network
(no authentication). If the first byte of the password is `\`\0\``,the network
is an unsecured open network.

Returns the *shape* of the credential, never the credential itself, so a caller
can render the right controls without the passphrase leaving the profile.
`WiFiSecurity::UNKNOWN` is a third state on purpose: a mutex failure is not an
answer, and folding it into either `WiFiSecurity::OPEN` or
`WiFiSecurity::PASSWORD` would make one of them a lie.

### Credentials

```cpp
bool getSsid(char* buf, size_t len) const;
bool setSsid(const char* ssid);

bool getPassword(char* buf, size_t len) const;
bool setPassword(const char* password);

bool setWiFiCredentials(const WiFiCredentials& creds);
bool getWiFiCredentials(WiFiCredentials& creds) const;
```

All methods are thread-safe. `getSsid()` / `getPassword()` return `false`
on truncation or mutex failure. An empty password is accepted and treated
as an open network (no authentication).

`setWiFiCredentials()` / `getWiFiCredentials()` update both SSID and password
atomically in a single mutex acquisition.

### TX power

```cpp
float getTxPower() const;
bool  setTxPower(float dbm);
```

`getTxPower()` returns the TX power in dBm, or `NAN` on mutex failure.
`setTxPower()` rejects anything the radio cannot actually produce; it never
adjusts the value to fit.

**The two platforms differ, and a caller building a UI needs to know which it is
on.** Both refuse a value off the 0.25 dBm grid rather than rounding it, because
a silently altered transmit power is indistinguishable from one that was
applied. On top of that, ESP32 offers only a dozen discrete levels and nothing
in between, so being on the grid is necessary but not sufficient there. On
ESP8266 the whole grid within range is available.

|Platform|Accepted values|Suitable control|
|--|--|--|
|ESP32|The `WIFI_TX_POWER_LEVEL_COUNT` entries of `WIFI_TX_POWER_LEVELS_Q`, in dBm: −1, 2, 5, 7, 8.5, 11, 13, 15, 17, 18.5, 19, 19.5|A drop-down|
|ESP8266|Any multiple of 0.25 dBm in [`MIN_WIFI_TX_POWER_dBm`, `MAX_WIFI_TX_POWER_dBm`]|A number input with `step="0.25"`|

The ESP32 levels are not written out in the library; they are derived from the
Arduino core's `wifi_power_t` enum, so a core release that moves a level moves
this list with it.

> The ESP32 radio itself does not reject an unsupported value — the IDF maps it
> down to the nearest level below and reports success. That silent step down is
> exactly what `setTxPower()` exists to catch.

Usage example:

```cpp
// Preferred: the level is named, so a typo is a compile error rather than a
// value the radio silently steps down from. The number is never written out,
// so a core update that moves a level moves this with it.
cfg.txPower = WIFI_POWER_13dBm * WiFiProfile::WIFI_TX_POWER_STEP_dBm;
```

### WiFiProfile bulk configuration

```cpp
bool setConfig(const WiFiConfig& cfg);
bool getConfig(WiFiConfig& cfg) const;
```

Extends the base `setConfig()` / `getConfig()` with SSID, password, and
TX power. All WiFi-specific fields are validated before the mutex is acquired.

### Static validators

```cpp
static bool        isValidSsid(const char* ssid);
static bool        isValidPassword(const char* password);
static bool        isValidTxPower(float dbm);
static ConfigCheck checkConfig(const WiFiConfig& cfg);
static bool        isValidConfig(const WiFiConfig& cfg);
```

**`isValidSsid()`** — enforces length [1, `MAX_SSID_LEN`]. Character content
is not restricted (SSID is a binary field per IEEE 802.11).

**`isValidPassword()`** — accepts empty string (open network). Non-empty
passwords must be 8–63 printable ASCII characters (0x20–0x7E) per WPA2-PSK.

**`isValidTxPower()`** — on ESP32, tests membership of the supported level set
(`WIFI_TX_POWER_LEVELS_Q`); on ESP8266, tests the range and the 0.25 dBm grid.
See [TX power](#tx-power) for why these are different questions.

**`checkConfig()`** — extends the base `checkConfig()` with the SSID, password
and TX-power checks (`BadSsid` / `BadPassword` / `BadTxPower`), then defers to it
for the rest. Returns `ConfigCheck`.

**`isValidConfig()`** — whole WiFi-config validity as a `bool`; wraps
`checkConfig()`.

**`NetworkProfile::configCheckName()`** — names a `ConfigCheck` value for logs and
configuration UIs, so a caller can report *which* field was refused without
writing the same switch every time. The string is flash-resident, so printing it
costs no RAM on AVR:

```cpp
if (!wifiProfile.setConfig(cfg)) {
    Serial.print(F("rejected: "));
    Serial.println(NetworkProfile::configCheckName(WiFiProfile::checkConfig(cfg)));
}
```

It names the field, not the remedy — a UI knows better than this library how to
phrase advice to its user.

`NtpProfile` and `MqttProfile` each carry their own `ConfigCheck` enum and their
own `configCheckName()`. The three are separate types, so use whichever belongs
to the profile that refused.

---

## EthProfile class

Extends `NetworkProfile` for Ethernet interfaces. No additional fields on
ESP32/ESP8266/AVR.

### EthProfile constructors

```cpp
EthProfile();
explicit EthProfile(const NetworkConfig& cfg);
```

---

## NtpProfile class

The NTP servers a device synchronises from. A standalone class in this library,
like `MqttProfile` — it does not extend `NetworkProfile`.

Device-level on purpose: a device has one clock, so the servers belong to it and
not to whichever interface is up. Where interfaces genuinely reach different time
servers — an internal one on a wired LAN, a public pool over a hotspot — keep two
instances and hand the manager whichever applies, from its CONNECTED handler.
That takes effect for the handoff in progress: the manager dispatches events to
the application earlier in the same `update()` pass than it configures NTP.

Two objects say that plainly; a field inside each interface's configuration said
it by accident, and usually said it twice with the same value.

Storage and validation only: no client, no protocol. NetworkManager applies the
servers when an interface comes up.

```cpp
#include <NtpProfile.h>
NtpProfile ntp;
```

### NtpProfile constants

|Constant|Default|Meaning|
|---|---|---|
|`SERVER_COUNT`|3 (ESP32), 1 (ESP8266), 1 (AVR)|stored servers|
|`MUTEX_TIMEOUT`|1000|ms a bulk operation waits for the lock (ESP32)|

`NTP_PROFILE_ENABLED` decides whether this class exists at all, and guards
NetworkManager's SNTP support with it — on AVR a UDP client with its own socket
and state machine, which is why NTP is off there by default. Declaring no
instance already costs nothing, so setting it to 0 is worth it only to drop the
manager's side as well.

`NTP_PROFILE_SERVER_COUNT` says how many servers a profile holds: 1 to 3, three
only on ESP32 by default since each costs `Host::MAX_FQDN_SIZE` of RAM. It is a
count rather than a switch, so zero is refused with a message naming
`NTP_PROFILE_ENABLED`.

### NtpConfig struct

```cpp
struct NtpConfig {
    char server[SERVER_COUNT][Host::MAX_FQDN_SIZE] = {};
};
```

Each entry is an FQDN or an IP literal; empty means the slot is unused. Order is
kept as entered, so a slot may be empty with a later one filled — the manager
compacts them when handing them to SNTP.

### NtpProfile ConfigCheck enum

|Value|Meaning|
|---|---|
|`Ok`|usable, including "no servers at all"|
|`BadServer`|an entry is neither a valid FQDN nor an IP literal|

### NtpProfile methods

```cpp
bool getConfig(NtpConfig& out) const;
bool setConfig(const NtpConfig& in);
bool getServer(uint8_t index, char* out, size_t len) const;
bool setServer(const char* str, uint8_t index = 0);
bool isConfigured() const;
virtual ConfigCheck checkConfig(const NtpConfig& cfg) const;
bool saveCfg(const char* ns);
bool loadCfg(const char* ns);
virtual bool toJson(char* json, size_t len) const;
bool importLegacy(const char* legacyNs);
```

`setServer()` rejects an address that is neither an FQDN nor an IP literal;
passing `nullptr` or an empty string clears the slot. `setConfig()` applies the
same rule to the whole struct through `checkConfig()`, so which of the two a
caller reaches for does not decide whether the check happens; nothing is stored
unless the struct passes. `configCheckName()` turns a `ConfigCheck` into text.

`checkConfig()` takes the config as a parameter, as `NetworkProfile::checkConfig()`
does, so it validates a staged copy — but it is virtual rather than static, since
a static one called through a base reference silently skips a subclass's own
rules. `setConfig()` calls it virtually, so those rules apply there too.

Having no servers at all passes validation: that is a device told to take its
time from DHCP, or from nowhere.

`loadCfg()` and `importLegacy()` do not validate what they read — a namespace
written by an older build, or edited by hand, is loaded as it stands. The
tightening is on the writing side, where a new value arrives.

**`importLegacy()`** reads servers written by a version that stored them per
interface, under `ntp0`..`ntpN` in the network profile's own namespace. Without
it an upgraded device would come back with no time servers and no indication why.
Save to the new namespace afterwards. **Scheduled for removal in 1.0.0.**

---

## MqttProfile class

Stores the settings a device needs in order to reach an MQTT broker. It holds no
client, no topics and no connection logic: the application connects, this only
survives the reboot. Topics are deliberately absent — a project derives them from
its own naming, and a profile that guessed at a layout would be wrong for most.

Unlike the interface profiles, `MqttProfile` does not extend `NetworkProfile`;
it is a standalone class in the same library.

```cpp
#include <MqttProfile.h>
MqttProfile mqtt;
```

### MqttProfile constants

| Constant | Default | Meaning |
| --- | --- | --- |
| `MAX_USER_LEN` / `MAX_USER_SIZE` | 32 / 33 | username, excluding / including the terminator |
| `MAX_PASSWORD_LEN` / `MAX_PASSWORD_SIZE` | 64 / 65 | password, likewise |
| `DEFAULT_PORT` | 1883 | plain MQTT |
| `DEFAULT_TLS_PORT` | 8883 | MQTT over TLS |
| `MUTEX_TIMEOUT` | 1000 | ms a bulk operation waits for the lock (ESP32) |

Override `MQTT_PROFILE_MAX_USER_LEN`, `MQTT_PROFILE_MAX_PASSWORD_LEN` and
`MQTT_PROFILE_MUTEX_TIMEOUT` through build flags.

### MqttConfig struct

```cpp
struct MqttConfig {
    bool     enabled  = false;
    char     host[Host::MAX_FQDN_SIZE] = {};   // FQDN or IP literal
    uint16_t port     = DEFAULT_PORT;
    bool     tls      = false;
    char     user[MAX_USER_SIZE]         = {}; // empty: anonymous
    char     password[MAX_PASSWORD_SIZE] = {};
};
```

`enabled` is a field of its own rather than an empty host standing in for it:
"switched off" and "not configured yet" are two different facts, and a device
taken off the broker for a while should not have to forget its address to do it.

Changing `tls` does not change `port`. The two well-known ports are offered as
constants, but guessing would overwrite a deliberate choice.

### MqttProfile ConfigCheck enum

| Value | Meaning |
| --- | --- |
| `Ok` | usable as it stands |
| `NoHost` | enabled, but no broker address |
| `BadHost` | not a valid FQDN or IP literal |
| `BadPort` | port is zero |
| `PasswordWithoutUser` | a password with no username to go with it |

### MqttProfile configuration

```cpp
bool getConfig(MqttConfig& out) const;
bool setConfig(const MqttConfig& in);
```

Both return `false` if the lock could not be obtained within `MUTEX_TIMEOUT`; on
failure `getConfig()` leaves `out` untouched and `setConfig()` changes nothing.
The lock matters because the struct is copied field by field: a portal saving
while a client task reads would otherwise hand out half of each.

`setConfig()` validates the struct through `checkConfig()` and stores nothing
unless it passes, so a rejected call leaves the previous settings intact. The
stage-check-keep pattern still works; it just no longer matters whether the
check was remembered. `configCheckName()` turns the result into text.

### MqttProfile validation

```cpp
virtual ConfigCheck checkConfig(const MqttConfig& cfg) const;
```

Takes the config as a parameter, like `NetworkProfile::checkConfig()`, so it
validates a staged copy and touches no member state. A disabled profile always
passes: its fields are not going to be used, and refusing to save a half-filled
form the user has just switched off would only be in the way.

Virtual rather than static, so a subclass's rules also run through a base
reference — see [Extending it](#extending-mqttprofile).

### MqttProfile persistence

```cpp
bool saveCfg(const char* ns);
bool loadCfg(const char* ns);
```

Same shape as `NetworkProfile`: the namespace is the caller's, so one device can
keep several brokers' settings side by side. A record is marked complete only
after everything is written, so a save interrupted by a reset is ignored on the
next load rather than half-applied.

An empty string removes its key instead of storing a blank, and `loadCfg()`
resets every field to its default before reading, so a load restores exactly
what the last save held — an emptied broker address stays empty even when
loading into an object that still has the old one.

### MqttProfile serialisation

```cpp
virtual bool toJson(char* json, size_t len) const;
```

For backup. **The password is not included** — a backup file travels through mail
and cloud storage, and a broker credential in it is a credential leaked. The
Wi-Fi profile makes the same trade: restoring one needs the password typed again.

Returns `false` and empties `json` if the document did not fit, rather than
leaving half of one.

### Extending MqttProfile

A project that stores one more setting subclasses rather than patching the
library:

```cpp
class HaMqttProfile : public MqttProfile {
public:
    struct HaConfig : public MqttConfig { bool discovery = false; };
    using MqttProfile::checkConfig;                     // keep the base overload visible
    ConfigCheck checkConfig(const HaConfig& cfg) const {
        if (cfg.discovery && !cfg.enabled) return ConfigCheck::NoHost;
        return MqttProfile::checkConfig(static_cast<const MqttConfig&>(cfg));
    }
};
```

Override `_doSave()` / `_doLoad()` (calling the base first) to persist the extra
field, and `toJson()` to include it.

See `examples/MqttConfig`.

---

## Passing a subclass config to a base function

`checkConfig()`, `isValidConfig()` and `setConfig()` refuse a subclass's config
at compile time:

```cpp
WiFiProfile::WiFiConfig cfg;
NetworkProfile& p = wifiProfile;
p.checkConfig(cfg);            // error: use of deleted function
```

The reason is that the base versions are static: the compiler picks one by the
declared type at the call site, so this used to check the addressing, skip the
SSID, password and TX-power rules, and report `Ok`. Deleted derived-type
overloads turn that silent half-check into an error.

Two ways forward:

```cpp
WiFiProfile::checkConfig(cfg);                              // what was almost always meant
NetworkProfile::checkConfig(static_cast<const NetworkProfile::NetworkConfig&>(cfg));
```

the second saying explicitly that only the addressing is of interest.

Copying a subclass config into a `NetworkConfig` variable first still loses the
extra fields — by then they are gone, and no signature can catch it.
