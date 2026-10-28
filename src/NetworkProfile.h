/*
 * Header-only: the Arduino IDE compiles libraries separately from the sketch,
 * so a precompiled unit would not see the configuration macros you define —
 * being header-only, the library is compiled with each includer's macros
 * instead. In a multi-file project, define those macros globally so every
 * translation unit agrees (see the README).
 */

#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <type_traits>
#include "NetworkProfileVersion.h"
#include "Host.h"

// The NTP servers moved to NtpProfile, but the macro that says whether NTP
// exists at all is needed by anything that builds on this family — and a
// quoted include cannot reach across libraries, while <detail/...> is generic
// enough to collide with another library's detail directory. Pulling it in from
// the front door every consumer already includes avoids both.
#include "detail/ntp_config.h"
#include "detail/json_tools.h"
#include "detail/prefs_tools.h"   // brings in Preferences (or its AVR stand-in)

#if defined(ARDUINO_ARCH_AVR)
#  include <avr/boot.h>
#elif !defined(ARDUINO_ARCH_ESP8266)
#  include <mutex>
#  include <chrono>
#endif
#if defined(ARDUINO_ARCH_ESP32)
#  include <esp_mac.h>   // esp_read_mac(ESP_MAC_BASE): the default hostname
#endif

#ifndef NETWORK_PROFILE_MUTEX_TIMEOUT
#  define NETWORK_PROFILE_MUTEX_TIMEOUT 1000
#endif

#if defined(ARDUINO_ARCH_AVR)
#  define NETWORK_PROFILE_MAX_DNS_SERVERS 1     // DNSClient limitation
#  define NETWORK_PROFILE_DEFAULT_DNS_SERVERS 1
#elif defined(ARDUINO_ARCH_ESP8266)
#  define NETWORK_PROFILE_MAX_DNS_SERVERS 2
#  define NETWORK_PROFILE_DEFAULT_DNS_SERVERS 2
#elif defined(ARDUINO_ARCH_ESP32)
#  define NETWORK_PROFILE_MAX_DNS_SERVERS 3     // LwIP configuration
#  define NETWORK_PROFILE_DEFAULT_DNS_SERVERS 2 // Limited to 2 by WiFi API
#endif

#ifndef NETWORK_PROFILE_DNS_SERVER_COUNT
#  define NETWORK_PROFILE_DNS_SERVER_COUNT NETWORK_PROFILE_DEFAULT_DNS_SERVERS
#endif

#if (NETWORK_PROFILE_DNS_SERVER_COUNT < 1) || (NETWORK_PROFILE_DNS_SERVER_COUNT > NETWORK_PROFILE_MAX_DNS_SERVERS)
#  error "NETWORK_PROFILE_DNS_SERVER_COUNT must be in [1-NETWORK_PROFILE_MAX_DNS_SERVERS] range."
#endif


#ifndef NETWORK_PROFILE_HOSTNAME_LEN
#  define NETWORK_PROFILE_HOSTNAME_LEN 63 // RFC 1123
#endif

#if (NETWORK_PROFILE_HOSTNAME_LEN < 10) || (NETWORK_PROFILE_HOSTNAME_LEN > 63) // Minimum 10 for generated default
#  error "NETWORK_PROFILE_HOSTNAME_LEN must be in [10-63] range"
#endif

#define NETWORK_PROFILE_MAC_LEN 6

/**
 * @brief Abstract base class for network interface configuration profiles.
 *
 * Stores common IP configuration (DHCP flag, IP address, subnet mask,
 * gateway, DNS servers), device-level settings (hostname, interface priority,
 * MAC address), and provides thread-safe load/save via Preferences.
 *
 * Subclasses extend _doSave(), _doLoad(), _doJson() and _initMacDefault()
 * to handle interface-specific fields (e.g. WiFi credentials, ETH MAC type).
 *
 * Platform support:
 *   - ESP32:   std::timed_mutex
 *   - ESP8266: cooperative scheduling, _lock()/_unlock() are no-ops
 *   - AVR:     single-threaded, _lock()/_unlock() are no-ops; saveCfg() and
 *              loadCfg() go through EEPROMPreferences, which presents the same
 *              interface as the ESP Preferences library
 */
class NetworkProfile {
public:

    /** @brief Number of DNS servers stored per profile. */
    static constexpr uint8_t  DNS_SERVER_COUNT  = NETWORK_PROFILE_DNS_SERVER_COUNT;

    /** @brief MAC address length in bytes. */
    static constexpr uint8_t  MAC_LEN           = NETWORK_PROFILE_MAC_LEN;

    /** @brief MAC address string buffer size: "AA:BB:CC:DD:EE:FF\0" */
    static constexpr size_t   MAC_STR_SIZE       = 18;

    /** @brief Default mutex acquisition timeout in milliseconds. */
    static constexpr uint32_t MUTEX_TIMEOUT      = NETWORK_PROFILE_MUTEX_TIMEOUT;

    /** @brief Maximum hostname length in bytes, excluding null terminator. */
    static constexpr size_t   MAX_HOSTNAME_LEN   = NETWORK_PROFILE_HOSTNAME_LEN;

    /** @brief Buffer size for hostname including null terminator. */
    static constexpr size_t   MAX_HOSTNAME_SIZE  = MAX_HOSTNAME_LEN + 1;

    /**
     * @brief Sentinel value returned by getPriority() on mutex timeout.
     *
     * A value of 255 indicates that the priority could not be read due to
     * a mutex acquisition failure. Valid priorities are in [0, 254].
     */
    static constexpr uint8_t  PRIORITY_INVALID   = 255;

#ifndef NETWORK_PROFILE_JSON_LEN
    /** @brief Maximum JSON content length in bytes, excluding null terminator.
     *
     * Everything this library itself can emit fits in 482 bytes, the worst case
     * being a WiFiProfile on ESP32 with three DNS servers:
     * - dhcp, priority and all IP fields in dotted-decimal form
     * - a 63-character hostname
     * - a MAC address ("AA:BB:CC:DD:EE:FF")
     * - an SSID whose 32 bytes are all escaped as \uXXXX → 192 chars
     * - txpwr, printed with two decimals
     * An EthProfile needs 266. The figure was ~1150 while every profile also
     * carried up to three NTP servers of its own; those moved to NtpProfile,
     * which sizes its own document.
     *
     * The default stays at 1200 rather than dropping to match. The headroom is
     * now for subclasses: _doJson() exists so a profile can add fields, and
     * those are counted against this same constant. Lowering it would be an
     * invisible constraint on code this library cannot see. Applications that
     * do not subclass and want the RAM back can set NETWORK_PROFILE_JSON_LEN to
     * 512 and still have margin.
     *
     * Override by defining NETWORK_PROFILE_JSON_LEN before including
     * this header. Use JSON_SIZE for the actual buffer declaration.
     */
    static constexpr size_t   JSON_LEN  = 1200;
#else
    static constexpr size_t   JSON_LEN  = NETWORK_PROFILE_JSON_LEN;
#endif

    /** @brief Buffer size for JSON output including null terminator.
     *
     * Use this constant when declaring a buffer for toJson():
     * @code
     * char json[NetworkProfile::JSON_SIZE];
     * profile.toJson(json, sizeof(json));
     * @endcode
     */
    static constexpr size_t   JSON_SIZE = JSON_LEN + 1;

    /** @brief MAC address type: array of 6 bytes. */
    using MACAddress = uint8_t[MAC_LEN];

    /**
     * @brief Interface type tag, returned by getInterfaceType().
     */
    enum class InterfaceType : uint8_t {
        UNKNOWN = 0,
        WIFI    = 1,
        ETH     = 2,
    };

    /**
     * @brief Selects between the active (user-set) and default (generated)
     *        value for fields that have a fallback.
     *
     * Used by getMac() and getHostname() to select which value to return:
     * - ACTIVE: returns the user-set override if present, otherwise FACTORY.
     * - FACTORY: always returns the generated/factory value.
     *
     * Calling these methods without a source argument defaults to ACTIVE,
     * which is the most common use case.
     */
    enum class ConfigSource : uint8_t {
        ACTIVE  = 0,
        FACTORY = 1,
    };

    // ------------------------------------------------------------------------
    // Bulk configuration struct
    // ------------------------------------------------------------------------

    /**
     * @brief Aggregates all profile configuration fields for atomic get/set.
     *
     * Used with setConfig() / getConfig() to update or retrieve the full
     * profile configuration atomically in a single mutex acquisition.
     * Prefer this over individual setters when multiple fields change at once
     * (e.g. when applying a configuration received from a web interface or
     * MQTT).
     *
     * @note setConfig() validates the whole struct before storing any of it, so
     *       a rejected field leaves the profile untouched rather than half
     *       applied. Use checkConfig() to learn which field failed.
     * @note The mac[] field sets the active MAC override. To clear the
     *       override and revert to the default MAC, set all bytes to 0x00.
     */
    struct NetworkConfig {
        bool        dhcp     = true;
        IPAddress   ip;
        IPAddress   mask;
        IPAddress   gateway;
        IPAddress   dns[DNS_SERVER_COUNT]                      = {};
        char        hostname[MAX_HOSTNAME_SIZE]                = {};
        uint8_t     priority = 0;
        MACAddress  mac      = {};
    };

    // ------------------------------------------------------------------------
    // Constructors
    // ------------------------------------------------------------------------

    /**
     * @brief Constructs a NetworkProfile with default values.
     *
     * DHCP is enabled, all IP fields are 0.0.0.0, the hostname is empty and
     * priority is 0. The default MAC is generated by _initMacDefault(), which
     * is called from the subclass constructor.
     */
    NetworkProfile()
        : _dhcp(true)
        , _ip(IPAddress(0, 0, 0, 0))
        , _mask(IPAddress(0, 0, 0, 0))
        , _gateway(IPAddress(0, 0, 0, 0))
        , _dns{}
        , _hostname{}
        , _priority(0)
        , _mac{}
        , _macDefault{}
        , _macDefaultKnown(false)
        , _hostnameDefault{}
    {}

    /**
     * @brief Constructs a NetworkProfile from a NetworkConfig struct.
     *
     * Equivalent to calling the default constructor followed by setConfig().
     * The default MAC is generated by _initMacDefault() which is called
     * from the subclass constructor — before setConfig() is applied.
     *
     * @param cfg  Initial configuration to apply.
     */
    explicit NetworkProfile(const NetworkConfig& cfg)
        : NetworkProfile()
    {
        setConfig(cfg);
    }

    /** @brief Destructor. */
    virtual ~NetworkProfile() = default;

    // Non-copyable
    NetworkProfile(const NetworkProfile&)            = delete;
    NetworkProfile& operator=(const NetworkProfile&) = delete;

    // ------------------------------------------------------------------------
    // Interface type
    // ------------------------------------------------------------------------

    /**
     * @brief Returns the interface type.
     *
     * Implemented by each subclass to return a compile-time constant.
     * No mutex needed — the value never changes and has no shared
     * mutable state.
     */
    virtual InterfaceType getInterfaceType() const = 0;

    // ------------------------------------------------------------------------
    // Getters / Setters — all thread-safe
    // ------------------------------------------------------------------------

    /** @brief Returns true if DHCP is enabled. */
    bool isDhcp() const {
        if (!_lock()) return false;
        bool v = _dhcp;
        _unlock();
        return v;
    }

    /** @brief Enables or disables DHCP. */
    bool setDhcp(bool dhcp) {
        if (!_lock()) return false;
        _dhcp = dhcp;
        _unlock();
        return true;
    }

    /** @brief Returns the static IP address. */
    IPAddress getIp() const {
        if (!_lock()) return IPAddress();
        IPAddress v = _ip;
        _unlock();
        return v;
    }

    /** @brief Sets the static IP address. */
    bool setIp(IPAddress ip) {
        if (!_lock()) return false;
        _ip = ip;
        _unlock();
        return true;
    }

    /** @brief Returns the subnet mask. */
    IPAddress getMask() const {
        if (!_lock()) return IPAddress();
        IPAddress v = _mask;
        _unlock();
        return v;
    }

    /** @brief Sets the subnet mask. */
    bool setMask(IPAddress mask) {
        if (!_lock()) return false;
        _mask = mask;
        _unlock();
        return true;
    }

    /** @brief Returns the default gateway. */
    IPAddress getGateway() const {
        if (!_lock()) return IPAddress();
        IPAddress v = _gateway;
        _unlock();
        return v;
    }

    /** @brief Sets the default gateway. */
    bool setGateway(IPAddress gateway) {
        if (!_lock()) return false;
        _gateway = gateway;
        _unlock();
        return true;
    }

    /**
     * @brief Returns a DNS server address by index.
     * @param index Index in [0, DNS_SERVER_COUNT). Clamped to 0 if out of
     *              range. Defaults to 0 (primary DNS).
     * @return DNS server address, or 0.0.0.0 on mutex failure.
     */
    IPAddress getDns(uint8_t index = 0) const {
        if (index >= DNS_SERVER_COUNT) index = 0;
        if (!_lock()) return IPAddress();
        IPAddress v = _dns[index];
        _unlock();
        return v;
    }

    /**
     * @brief Sets a DNS server address by index.
     * @param dns   DNS server address to set.
     * @param index Index in [0, DNS_SERVER_COUNT). Clamped to 0 if out of
     *              range. Defaults to 0 (primary DNS).
     * @return true on success, false on mutex timeout.
     */
    bool setDns(IPAddress dns, uint8_t index = 0) {
        if (index >= DNS_SERVER_COUNT) index = 0;
        if (!_lock()) return false;
        _dns[index] = dns;
        _unlock();
        return true;
    }

    /**
     * @brief Copies the hostname into the provided buffer.
     *
     * If @p source is ConfigSource::ACTIVE and the active hostname is set,
     * it is returned. If the active hostname is empty, the generated default
     * hostname is returned instead (same behaviour as ACTIVE with fallback).
     * If @p source is ConfigSource::FACTORY, the generated hostname is always
     * returned regardless of the active hostname.
     *
     * @param buf    Destination buffer.
     * @param len    Size of the destination buffer in bytes.
     * @param source ConfigSource::ACTIVE (default) or ConfigSource::FACTORY.
     * @return true if the hostname fit entirely, false on truncation or
     *         mutex failure.
     */
    bool getHostname(char* buf, size_t len,
                     ConfigSource source = ConfigSource::ACTIVE) const {
        if (!buf || len == 0) return false;
        if (!_lock()) return false;

        const char* src = _hostname;
        if (source == ConfigSource::FACTORY || _hostname[0] == '\0') {
            src = _hostnameDefault;
        }

        strncpy(buf, src, len);
        buf[len - 1] = '\0';
        bool fit = (strlen(src) < len);
        _unlock();
        return fit;
    }

    /**
     * @brief Sets the active hostname after RFC 1123 validation.
     * @note Empty string resets the active hostname. getHostname()
     *       will return the default one.
     * @param hostname Null-terminated hostname string.
     * @return true on success, false on invalid hostname or mutex failure.
     */
    bool setHostname(const char* hostname) {
        if (hostname[0] != '\0' && !isValidHostname(hostname)) return false;
        if (!_lock()) return false;
        strncpy(_hostname, hostname, MAX_HOSTNAME_LEN);
        _hostname[MAX_HOSTNAME_LEN] = '\0';
        _unlock();
        return true;
    }

    /**
     * @brief Validates a hostname string against RFC 1123 rules.
     *
     * Rules enforced:
     * - Length must be between 1 and MAX_HOSTNAME_LEN characters.
     * - Only ASCII letters, digits, and hyphens are allowed.
     * - Must not start or end with a hyphen.
     * - Must not be all-numeric.
     *
     * @param hostname Null-terminated string to validate.
     * @return true if valid, false otherwise.
     */
    static bool isValidHostname(const char* hostname) {
        if (!hostname || hostname[0] == '\0') return false;
        size_t len = strlen(hostname);
        if (len > MAX_HOSTNAME_LEN) return false;
        if (hostname[0] == '-' || hostname[len - 1] == '-') return false;
        bool allNumeric = true;
        for (size_t i = 0; i < len; i++) {
            char c = hostname[i];
            if (!isalnum((unsigned char)c) && c != '-') return false;
            if (!isdigit((unsigned char)c)) allNumeric = false;
        }
        if (allNumeric) return false;
        return true;
    }


    /**
     * @brief Returns the interface priority.
     *
     * Lower value = higher priority. Valid range: [0, 254].
     *
     * @return Priority value, or PRIORITY_INVALID (255) on mutex timeout.
     */
    uint8_t getPriority() const {
        if (!_lock()) return PRIORITY_INVALID;
        uint8_t v = _priority;
        _unlock();
        return v;
    }

    /**
     * @brief Sets the interface priority.
     *
     * @param priority Priority value in [0, 254]. Value 255 is reserved
     *                 as PRIORITY_INVALID and will be rejected.
     * @return true on success, false on invalid value or mutex timeout.
     */
    bool setPriority(uint8_t priority) {
        if (priority == PRIORITY_INVALID) return false;
        if (!_lock()) return false;
        _priority = priority;
        _unlock();
        return true;
    }

    // ------------------------------------------------------------------------
    // MAC address
    // ------------------------------------------------------------------------

    /**
     * @brief Copies the MAC address into the provided buffer.
     *
     * If @p source is ConfigSource::ACTIVE and an active MAC override is set
     * (hasMacOverride()), it is returned. Otherwise the factory MAC is
     * returned instead (ACTIVE with fallback). If @p source is
     * ConfigSource::FACTORY, the factory MAC is always returned.
     *
     * The factory MAC can be unknown (hasFactoryMac()): on an interface whose
     * address lives on a co-processor it is learnt only once the interface has
     * started. Asked for it then, this fills @p mac with zeros and returns
     * false rather than handing back an address nobody owns.
     *
     * @param mac    Destination buffer (MAC_LEN bytes).
     * @param source ConfigSource::ACTIVE (default) or ConfigSource::FACTORY.
     * @return true on success; false on mutex timeout (@p mac untouched) or
     *         when the address asked for is the factory one and it is not yet
     *         known (@p mac zeroed).
     */
    bool getMac(MACAddress mac, ConfigSource source = ConfigSource::ACTIVE) const {
        if (!_lock()) return false;

        const bool useOverride = (source == ConfigSource::ACTIVE) && isValidMac(_mac);
        const bool known       = useOverride || _macDefaultKnown;
        if (known) memcpy(mac, useOverride ? _mac : _macDefault, MAC_LEN);
        else       memset(mac, 0, MAC_LEN);
        _unlock();
        return known;
    }

    /**
     * @brief Whether an active MAC override is set.
     *
     * An adapter does not need this to apply the address — it brings the
     * hardware to getMac(ACTIVE) and writes only when the two differ — but it
     * is what decides whether the override is stored at all (_doSave()).
     *
     * @return true if a valid override is set; false if not, or on mutex
     *         timeout.
     */
    bool hasMacOverride() const {
        if (!_lock()) return false;
        const bool set = isValidMac(_mac);
        _unlock();
        return set;
    }

    /**
     * @brief Whether the factory MAC is known.
     *
     * True from construction on every interface whose address the chip itself
     * holds. False on one whose address lives on a co-processor — WiFi on an
     * ESP32-P4, run by an ESP32-C6 over ESP-Hosted — until its adapter has
     * started the interface, read the address and reported it with
     * setFactoryMac().
     *
     * @return true if known; false if not, or on mutex timeout.
     */
    bool hasFactoryMac() const {
        if (!_lock()) return false;
        const bool known = _macDefaultKnown;
        _unlock();
        return known;
    }

    /**
     * @brief Records the factory MAC as the hardware reports it.
     *
     * For adapters only, and only for an interface whose factory MAC could
     * not be read when the profile was constructed (hasFactoryMac() false):
     * the adapter reads it from the driver once the interface is up and hands
     * it over here. Accepted once; a known factory MAC is not replaced.
     *
     * The default hostname does not change with it: on ESP32 that is built
     * from the chip's base MAC, which is known from the start.
     *
     * @param mac The address the hardware reports (MAC_LEN bytes).
     * @return true if recorded; false if @p mac is not valid, a factory MAC is
     *         already known, or on mutex timeout.
     */
    bool setFactoryMac(const MACAddress mac) {
        if (!isValidMac(mac)) return false;
        if (!_lock()) return false;
        const bool ok = !_macDefaultKnown;
        if (ok) {
            memcpy(_macDefault, mac, MAC_LEN);
            _macDefaultKnown = true;
        }
        _unlock();
        return ok;
    }

    /**
     * @brief Sets the active MAC address override.
     *
     * The MAC is validated with isValidMac() before storing.
     * To revert to the default MAC, set all bytes to 0x00 via setConfig().
     *
     * @param mac Source MAC address (MAC_LEN bytes).
     * @return true on success, false on invalid MAC or mutex timeout.
     */
    bool setMac(const MACAddress mac) {
        if (!isValidMac(mac)) return false;
        if (!_lock()) return false;
        memcpy(_mac, mac, MAC_LEN);
        _unlock();
        return true;
    }

    /**
     * @brief Serialises a MAC address to a human-readable string.
     *
     * Output format: "AA:BB:CC:DD:EE:FF" (uppercase hex, colon-separated).
     *
     * @param buf  Destination buffer (at least MAC_STR_SIZE bytes).
     * @param len  Size of destination buffer.
     * @param mac  Source MAC address.
     * @return true if the result fit entirely, false on truncation.
     */
    static bool macToStr(char* buf, size_t len, const MACAddress mac) {
        if (!buf || len < MAC_STR_SIZE) return false;
        snprintf_P(buf, len, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"),
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return true;
    }

    /**
     * @brief Parses a MAC address string into a MACAddress buffer.
     *
     * Accepts "AA:BB:CC:DD:EE:FF" format (case-insensitive).
     *
     * @param str  Null-terminated MAC address string.
     * @param mac  Destination buffer (MAC_LEN bytes).
     * @return true on success, false if the string is not a valid MAC.
     */
    static bool macFromStr(const char* str, MACAddress mac) {
        if (!str) return false;
        
        // Expected format: "AA:BB:CC:DD:EE:FF" — exactly 17 characters
        if (strlen(str) != 17) return false;
        
        for (uint8_t i = 0; i < MAC_LEN; i++) {
            // Each byte: two hex digits
            const char* p = str + i * 3;
            
            if (!isxdigit((unsigned char)p[0]) ||
                !isxdigit((unsigned char)p[1])) return false;
            
            // Separator check — except after last byte
            if (i < MAC_LEN - 1 && p[2] != ':') return false;
            
            // Parse two hex digits directly into uint8_t
            uint8_t hi = isdigit((unsigned char)p[0])
                    ? (p[0] - '0')
                    : (toupper((unsigned char)p[0]) - 'A' + 10);
            uint8_t lo = isdigit((unsigned char)p[1])
                    ? (p[1] - '0')
                    : (toupper((unsigned char)p[1]) - 'A' + 10);
            
            mac[i] = (hi << 4) | lo;
        }
        return true;
    }

    /**
     * @brief Checks whether a MAC address can be an interface's own address.
     *
     * Returns false if all bytes are 0x00 (unset), or if the I/G bit — the
     * lowest bit of byte 0 — is set: that marks a group (multicast) address,
     * which covers the all-0xFF broadcast address too. No interface can send
     * from one, and the drivers refuse it (esp_wifi_set_mac(),
     * wifi_set_macaddr()), so an override that passed here would only fail
     * later, at the adapter.
     *
     * Locally administered addresses (U/L bit set) are valid.
     *
     * @param mac MAC address to validate.
     * @return true if valid, false otherwise.
     */
    static bool isValidMac(const MACAddress mac) {
        if (mac[0] & 0x01) return false;        // group address, incl. broadcast
        for (uint8_t i = 0; i < MAC_LEN; i++) {
            if (mac[i] != 0x00) return true;
        }
        return false;                           // all zero: not set
    }

    /**
     * @brief A little helper to convert an IPAddress to the provided string buffer.
     * @param str  Destination string buffer.
     * @param len  Size of destination buffer.
     * @param ip   Source IP address.
     * @return true on success, false otherwise.
     */
    static bool ipToStr(char* str, size_t len, const IPAddress ip) {
        if (!str || len == 0) return false;
        if (ip == IPAddress()) {
            str[0] = '\0';
            return true;
        }
        int written = snprintf_P(str, len, PSTR("%d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
        if (written < 0 || (size_t)written >= len) return false;
        return true;
    }

    /**
     * @brief Result of checkStatic(): the first coherence problem, or Ok.
     */
    enum class ConfigCheck : uint8_t {
        Ok = 0,             ///< the config is valid
        BadHostname,        ///< hostname is set but is not a valid hostname
        BadIp,              ///< host ip is unusable (0/loopback/multicast/reserved) or the subnet network/broadcast address
        BadMask,            ///< mask is not a contiguous run of 1-bits
        GatewayOffSubnet,   ///< a non-zero gateway is not on the ip/mask subnet
        BadDns,             ///< a DNS entry is set but not a usable unicast address
        BadSsid,            ///< (WiFiProfile) ssid is empty or too long
        BadPassword,        ///< (WiFiProfile) password fails the WPA length rules
        BadTxPower,         ///< (WiFiProfile) txPower is not an allowed level
    };

    /**
     * @brief Names a ConfigCheck value, for logs and configuration UIs.
     *
     * checkConfig() exists so a caller can say *which* field was refused rather
     * than only that something was; without a name for the value, every caller
     * has to write the same switch. The text names the field, not the remedy —
     * a UI knows better than this library how to phrase advice to its user.
     *
     * Held in flash on AVR, so printing it costs no RAM:
     * @code
     * Serial.print(F("rejected: "));
     * Serial.println(NetworkProfile::configCheckName(profile.checkConfig(cfg)));
     * @endcode
     *
     * @param c Value to name.
     * @return Flash-resident string, printable with Serial.print().
     */
    static const __FlashStringHelper* configCheckName(ConfigCheck c) {
        switch (c) {
            case ConfigCheck::Ok:               return F("ok");
            case ConfigCheck::BadHostname:      return F("invalid hostname");
            case ConfigCheck::BadIp:            return F("invalid IP address");
            case ConfigCheck::BadMask:          return F("invalid subnet mask");
            case ConfigCheck::GatewayOffSubnet: return F("gateway outside the subnet");
            case ConfigCheck::BadDns:           return F("invalid DNS address");
            case ConfigCheck::BadSsid:          return F("invalid SSID");
            case ConfigCheck::BadPassword:      return F("invalid password");
            case ConfigCheck::BadTxPower:       return F("unsupported TX power");
        }
        return F("unknown");
    }

private:
    /// The address as a plain 32-bit value, most-significant octet first. Not
    /// IPAddress::operator uint32_t(), which is endianness-dependent and would
    /// make a valid mask look non-contiguous on a little-endian MCU.
    static uint32_t ipU32(const IPAddress& x) {
        return ((uint32_t)x[0] << 24) | ((uint32_t)x[1] << 16)
             | ((uint32_t)x[2] <<  8) |  (uint32_t)x[3];
    }

    /// Whether @p a could be a real unicast host/DNS/gateway address on an
    /// ESP interface. Rejects the ranges that never can be: 0.0.0.0/8
    /// (this-network), 127.0.0.0/8 (loopback — an ESP runs no local services),
    /// 224.0.0.0/4 (multicast) and 240.0.0.0/4 (reserved, which also covers the
    /// 255.255.255.255 limited broadcast).
    static bool isUnicastHost(uint32_t a) {
        if ((a & 0xFF000000u) == 0x00000000u) return false; // 0/8
        if ((a & 0xFF000000u) == 0x7F000000u) return false; // 127/8
        if ((a & 0xF0000000u) == 0xE0000000u) return false; // 224/4
        if ((a & 0xF0000000u) == 0xF0000000u) return false; // 240/4
        return true;
    }

    /// Coherence of the static-addressing block (ip, mask, gateway, DNS). Pure
    /// arithmetic, so it runs before the radio is up and works on an isolated
    /// segment with no gateway. Optional fields (gateway, DNS) are skipped when
    /// 0.0.0.0 (= not set); a non-zero value must be valid. Called by
    /// checkConfig() under static addressing.
    static ConfigCheck checkStatic(const NetworkConfig& cfg) {
        const uint32_t a = ipU32(cfg.ip);
        const uint32_t m = ipU32(cfg.mask);
        // A usable netmask is a non-empty, contiguous run of 1-bits then 0-bits:
        // for host mask h = ~m, h and h+1 share no bits.
        if (m == 0) return ConfigCheck::BadMask;
        const uint32_t host = ~m;
        if (host & (host + 1)) return ConfigCheck::BadMask;
        // The host address must be a usable unicast, and not the subnet's
        // network or broadcast address. /31 and /32 have no net/broadcast.
        if (!isUnicastHost(a)) return ConfigCheck::BadIp;
        if (host != 0 && host != 1) {
            const uint32_t h = a & host;
            if (h == 0 || h == host) return ConfigCheck::BadIp;
        }
        // A gateway, if given, must sit on the same subnet as the host.
        const uint32_t g = ipU32(cfg.gateway);
        if (g != 0 && ((g & m) != (a & m))) return ConfigCheck::GatewayOffSubnet;
        // DNS servers, if given, must be usable unicast addresses. They may be
        // off-subnet (a remote resolver), so there is no subnet test.
        for (uint8_t i = 0; i < DNS_SERVER_COUNT; i++) {
            const uint32_t d = ipU32(cfg.dns[i]);
            if (d != 0 && !isUnicastHost(d)) return ConfigCheck::BadDns;
        }
        return ConfigCheck::Ok;
    }

public:

    // ------------------------------------------------------------------------
    // Atomic bulk configuration
    // ------------------------------------------------------------------------

    /**
     * @brief Atomically sets all profile fields from a NetworkConfig struct.
     *
     * All base fields (IP, DNS, hostname, priority, MAC) are updated in a
     * single mutex acquisition.
     *
     * @param cfg Source configuration to apply.
     * @return true on success, false on mutex timeout.
     */
    bool setConfig(const NetworkConfig& cfg) {
        if (!isValidConfig(cfg)) return false;
        if (!_lock()) return false;
        _doSetConfig(cfg);
        _unlock();
        return true;
    }

    /**
     * @brief Atomically reads all profile fields into a NetworkConfig struct.
     *
     * The mac field in the returned struct contains the active MAC override
     * (may be all zeros if no override is set).
     *
     * @param cfg Destination structure to fill.
     * @return true on success, false on mutex timeout.
     */
    bool getConfig(NetworkConfig& cfg) const {
        if (!_lock()) return false;
        _doGetConfig(cfg);
        _unlock();
        return true;
    }

    /**
     * @brief Validates a whole config and names the first problem, or Ok.
     *
     * The detailed counterpart of isValidConfig(): identical checks, but it
     * returns *which* one failed so a caller can explain it. Static coherence is
     * checked only under static addressing; DHCP leaves ip/mask/gateway unused.
     * First failure wins.
     *
     * @param cfg Config to validate.
     * @return ConfigCheck::Ok, or the first problem found.
     */
    static ConfigCheck checkConfig(const NetworkConfig& cfg) {
        if (cfg.hostname[0] != '\0' && !isValidHostname(cfg.hostname))
            return ConfigCheck::BadHostname;
        // Static addressing must be coherent. Skipped under DHCP, where the
        // ip/mask/gateway/dns fields are unused.
        if (!cfg.dhcp) {
            ConfigCheck s = checkStatic(cfg);
            if (s != ConfigCheck::Ok) return s;
        }
        return ConfigCheck::Ok;
    }

    /**
     * @brief Refuses a subclass's config, so the omission cannot be silent.
     *
     * These functions are static, so the compiler picks one by the *declared*
     * type at the call site. Handing a WiFiConfig to the base version — which
     * happens the moment a caller holds a NetworkProfile& — would quietly check
     * only the addressing and skip the SSID, password and TX-power rules, and
     * report Ok. Deleting the derived-type overloads turns that into a
     * compile-time error instead.
     *
     * Two ways forward when it fires: call through the concrete type
     * (`WiFiProfile::checkConfig(cfg)`), which is almost always what was meant;
     * or, if only the addressing really is of interest, say so —
     * `checkConfig(static_cast<const NetworkConfig&>(cfg))`.
     *
     * Slicing a config into a NetworkConfig variable first is still possible and
     * still loses the subclass fields; no signature can catch that, since by then
     * they are gone.
     */
    template <class T, class = typename std::enable_if<
                  std::is_base_of<NetworkConfig, T>::value &&
                  !std::is_same<NetworkConfig, T>::value>::type>
    static ConfigCheck checkConfig(const T&) = delete;

    /** @copydoc checkConfig(const T&) */
    template <class T, class = typename std::enable_if<
                  std::is_base_of<NetworkConfig, T>::value &&
                  !std::is_same<NetworkConfig, T>::value>::type>
    static bool isValidConfig(const T&) = delete;

    /** @copydoc checkConfig(const T&) */
    template <class T, class = typename std::enable_if<
                  std::is_base_of<NetworkConfig, T>::value &&
                  !std::is_same<NetworkConfig, T>::value>::type>
    bool setConfig(const T&) = delete;

    /**
     * @brief Whole-config validity as a bool.
     *
     * The storability gate used by setConfig(); a thin wrapper over checkConfig()
     * kept for the many callers that only need yes/no. Making it authoritative
     * means an invalid config can never be stored, however the UI validated it.
     */
    static bool isValidConfig(const NetworkConfig& cfg) {
        return checkConfig(cfg) == ConfigCheck::Ok;
    }

    // ------------------------------------------------------------------------
    // Serialisation
    // ------------------------------------------------------------------------

    /**
     * @brief Serialises the full profile configuration to a JSON string.
     *
     * Output fields (base class):
     * dhcp (0/1), ip, mask, gw, dns[0, DNS_SERVER_COUNT) (dotted-decimal),
     * hostname, prio, mac (active MAC, "AA:BB:CC:DD:EE:FF")
     * 
     * Subclasses may override _doJson() to append interface-specific fields
     * (e.g. WiFiProfile appends "ssid").
     * 
     * If output has truncated anywhere it outputs an empty buffer (with `\0`
     * in json[0]) instead of a malformed JSON stucture.
     *
     * @param json Destination buffer.
     * @param len  Size of destination buffer in bytes.
     * @return true on success, false on mutex timeout or output truncated.
     */
    bool toJson(char* json, size_t len) const {
        if (!json || len == 0) return false;

        constexpr size_t BS = 80;
        char buf[BS];
        char macBuf[MAC_STR_SIZE];
        char ipStr[16];
        bool ok = true;

        json[0] = '\0';
        if (!_lock()) return false;
        ok = ok && _strncatJson_P(json, PSTR("{"), len);
        // DHCP
        ok = ok && _fitted(snprintf_P(buf, BS, PSTR("\"dhcp\":%u"), (unsigned)_dhcp), BS);
        ok = ok && _strncatJson(json, buf, len);
        if (!_dhcp) {
            // IP address
            ok = ok && ipToStr(ipStr, sizeof(ipStr), _ip);
            ok = ok && _fitted(snprintf_P(buf, BS, PSTR(",\"ip\":\"%s\""), ipStr), BS);
            ok = ok && _strncatJson(json, buf, len);
            // Netmask
            ok = ok && ipToStr(ipStr, sizeof(ipStr), _mask);
            ok = ok && _fitted(snprintf_P(buf, BS, PSTR(",\"mask\":\"%s\""), ipStr), BS);
            ok = ok && _strncatJson(json, buf, len);
            // Default gateway
            ok = ok && ipToStr(ipStr, sizeof(ipStr), _gateway);
            ok = ok && _fitted(snprintf_P(buf, BS, PSTR(",\"gw\":\"%s\""), ipStr), BS);
            ok = ok && _strncatJson(json, buf, len);
            // DNS servers
            for (uint8_t i = 0; i < DNS_SERVER_COUNT; i++) {
                ok = ok && ipToStr(ipStr, sizeof(ipStr), _dns[i]);
                ok = ok && _fitted(snprintf_P(buf, BS, PSTR(",\"dns%u\":\"%s\""), i, ipStr), BS);
                ok = ok && _strncatJson(json, buf, len);
            }
        }
        // Hostname
        ok = ok && _fitted(snprintf_P(buf, BS, PSTR(",\"host\":\"%s\""),
            _hostname[0] != '\0' ? _hostname : _hostnameDefault), BS);
        ok = ok && _strncatJson(json, buf, len);
        // Interface priority
        ok = ok && _fitted(snprintf_P(buf, BS, PSTR(",\"prio\":%u"), (unsigned)_priority), BS);
        ok = ok && _strncatJson(json, buf, len);

        // Active MAC — override if valid, otherwise default; empty while the
        // factory MAC is not yet known, rather than an address nobody owns
        const bool overridden = isValidMac(_mac);
        const uint8_t* macSrc = overridden ? _mac : _macDefault;
        if (overridden || _macDefaultKnown) {
            ok = ok && macToStr(macBuf, sizeof(macBuf), macSrc);
        } else {
            macBuf[0] = '\0';
        }
        ok = ok && _fitted(snprintf_P(buf, BS, PSTR(",\"mac\":\"%s\""), macBuf), BS);
        ok = ok && _strncatJson(json, buf, len);


        // JSON serialization hook for childs
        ok = ok && _doJson(json, len);
        ok = ok && _strncatJson_P(json, PSTR("}"), len);

        _unlock();

        // Empty string instead of a malformed JSON stucture.
        if (!ok) json[0] = '\0';
        return ok;
    }

    // ------------------------------------------------------------------------
    // Persistence
    // ------------------------------------------------------------------------

    /**
     * @brief Saves the full profile to non-volatile storage.
     *
     * Transactional: a completion marker is written last, so an interrupted
     * save (e.g. power loss) is rejected by loadCfg() and never loaded as a
     * valid profile. Single-buffered — a failed re-save therefore also
     * invalidates the previously stored profile (no rollback to the prior copy).
     * @param ns Preferences namespace (e.g. "net_wifi", "net_eth").
     * @return true on success, false on storage failure or mutex timeout.
     */
    bool saveCfg(const char* ns) {
        if (!ns) return false;
        if (!_lock()) return false;
        Preferences p;
        bool ok = p.begin(ns, _RW_MODE);
        if (ok) {
            ok = p.putBool("_ok", false);
            if (ok) ok = _doSave(p);
            if (ok) ok = p.putBool("_ok", true);
            p.end();
        }
        _unlock();
        return ok;
    }

    /**
     * @brief Loads the full profile from non-volatile storage.
     * @param ns Preferences namespace (e.g. "net_wifi", "net_eth").
     * @return true on success; false on an empty/never-written namespace, an
     *         incomplete save (torn write), a storage failure, or mutex
     *         timeout. Never loads a half-written profile.
     */
    bool loadCfg(const char* ns) {
        if (!ns) return false;
        if (!_lock()) return false;
        Preferences p;
        bool ok = p.begin(ns, _RO_MODE);
        if (ok) {
            ok = p.getBool("_ok", false);
            if (ok) ok = _doLoad(p);
            p.end();
        }
        _unlock();
        return ok;
    }

    /**
     * @brief Clears all saved profile data from non-volatile storage.
     *
     * Erases the entire Preferences namespace @p ns on every platform — AVR
     * included, where EEPROMPreferences::clear() does the same job. After
     * calling this, a reboot will load default values.
     *
     * @param ns Preferences namespace to clear.
     * @return true on success, false on failure or mutex timeout.
     */
    bool clearCfg(const char* ns) {
        if (!ns) return false;
        if (!_lock()) return false;
        Preferences p;
        bool ok = p.begin(ns, _RW_MODE);
        if (ok) { ok = p.clear(); p.end(); }
        _unlock();
        return ok;
    }

private:
    // ------------------------------------------------------------------------
    // Internal data
    // ------------------------------------------------------------------------

    bool          _dhcp;
    IPAddress     _ip;
    IPAddress     _mask;
    IPAddress     _gateway;
    IPAddress     _dns[DNS_SERVER_COUNT];
    char          _hostname[MAX_HOSTNAME_SIZE];
    uint8_t       _priority;
    uint8_t       _mac[NETWORK_PROFILE_MAC_LEN];

protected:
    // ------------------------------------------------------------------------
    // MAC initialisation hook — must be called from subclass constructor
    // ------------------------------------------------------------------------

    /**
     * @brief Initialises the default (factory/generated) MAC address.
     *
     * Must be implemented by each subclass to generate the platform- and
     * interface-specific default MAC:
     * - WiFiProfile/ESP32: esp_read_mac(ESP_MAC_WIFI_STA)
     * - EthProfile/ESP32:  esp_read_mac(ESP_MAC_ETH)
     * - ESP8266:           WiFi.macAddress()
     * - ATmega328PB:       boot_signature_byte_get() based generation
     * - Other AVR:         NETWORK_PROFILE_DEFAULT_ETH_MAC string via
     *                      macFromStr()
     *
     * Must be called explicitly at the end of each subclass constructor,
     * after the base constructor has completed.
     *
     * Sets _macDefaultKnown to whether the read succeeded. Where the address
     * cannot be read yet — WiFi on an ESP32-P4, which lives on an ESP-Hosted
     * co-processor — it stays false, _macDefault stays zero, and the adapter
     * supplies the address later through setFactoryMac().
     *
     * Also generates _hostnameDefault (see _generateHostnameDefault()), e.g.
     * "ESP-02D6FC" or "AVR-4E5230".
     */
    virtual void _initMacDefault() = 0;

    /**
     * @brief Generates _hostnameDefault.
     *
     * Called by subclasses at the end of _initMacDefault(), after _macDefault
     * has been populated. Format: "<PREFIX>-XXYYZZ" where XX, YY, ZZ are the
     * last three bytes of the source MAC in uppercase hex.
     *
     * The source is the chip's base MAC on ESP32 (esp_read_mac(ESP_MAC_BASE)),
     * and _macDefault elsewhere. The hostname names the device, not one of its
     * interfaces, so every profile on it has to come out the same, and on
     * ESP32 the interface addresses are not: Ethernet is base+3 with four
     * universal addresses, or a local one derived from base+1. The base is
     * also what the chip knows from boot on every ESP32 — including the P4,
     * whose WiFi address is not readable until the co-processor is up. On
     * ESP32 WiFi this changes nothing, since the station address is the base.
     *
     * Falls back to _macDefault if the base cannot be read; with that unknown
     * as well, the hostname carries zeros until one is configured.
     *
     * @param prefix Platform prefix string (e.g. "ESP", "AVR").
     */
    void _generateHostnameDefault(const char* prefix) {
        const uint8_t* src = _macDefault;
#if defined(ARDUINO_ARCH_ESP32)
        uint8_t base[MAC_LEN];
        if (esp_read_mac(base, ESP_MAC_BASE) == ESP_OK) src = base;
#endif
        snprintf(_hostnameDefault, MAX_HOSTNAME_SIZE, "%s-%02X%02X%02X",
            prefix, src[3], src[4], src[5]);
    }

    // ------------------------------------------------------------------------
    // Persistence hooks and helpers
    // ------------------------------------------------------------------------

    /**
     * @brief Saves a non-empty string buffer to persistent storage. Delete the
     *        key on empty string.
     * 
     * putString() returns the value length (0 for an empty string on ESP32),
     * so a plain "> 0" check wrongly reports a stored empty value as a failure.
     * Store non-empty values (requiring success); remove the key for empty ones
     * so a later load falls back to the field's default.
     * 
     * @param p Open, writable Preferences object.
     * @param key Target key name.
     * @return true if string was written successfully or on removed key due to
     *         empty string. false on any error.
     */
    static bool _putStr(Preferences& p, const char* key, const char* value) {
        return prefs_putStr(p, key, value);   // detail/prefs_tools.h
    }
    
    /**
     * @brief Saves profile-specific fields to an already-open Preferences
     *        object.
     *
     * Sets ip, mask, gw, dns* fields if dhcp is disabled, deletes them
     * otherwise. The transaction's completion marker is written by saveCfg()
     * after this returns, not here.
     * @param p Open, writable Preferences object.
     * @return true if all fields were written successfully, false on any error.
     */
    virtual bool _doSave(Preferences& p) {
        bool ok = true;
        ok = ok && p.putBool("dhcp", _dhcp);
        if (!_dhcp) {
            ok = ok && (p.putUInt("ip", (uint32_t)_ip)      > 0);
            ok = ok && (p.putUInt("mask", (uint32_t)_mask)  > 0);
            ok = ok && (p.putUInt("gw", (uint32_t)_gateway) > 0);
        } else {
            prefs_remove(p, "ip");
            prefs_remove(p, "mask");
            prefs_remove(p, "gw");
        }
        for (uint8_t i = 0; i < DNS_SERVER_COUNT; i++) {
            char key[8];
            snprintf(key, sizeof(key), "dns%u", i);
            if (!_dhcp)
                ok = ok && (p.putUInt(key, (uint32_t)_dns[i]) > 0);
            else
                prefs_remove(p, key);
        }
        ok = ok && (_putStr(p, "host", _hostname)         > 0);
        ok = ok && (p.putUChar( "prio", _priority)         > 0);
        // Only an override is stored. Without one the key is removed, so the
        // record says "use the factory address" rather than holding a copy of
        // zeros — the same convention as the static-address keys above.
        if (isValidMac(_mac))
            ok = ok && (p.putBytes( "mac",  _mac, MAC_LEN) == MAC_LEN);
        else
            prefs_remove(p, "mac");

        return ok;
    }

    /**
     * @brief Loads profile-specific fields from an already-open Preferences
     *        object.
     *
     * The namespace-presence and completed-transaction checks (the "_ok"
     * marker) are handled by loadCfg() before this runs; a subclass override
     * only needs to read its own fields.
     *
     * Every field is reset to its default before it is read, so what this
     * leaves behind depends only on the store and never on what the object
     * happened to hold. That is not cosmetic: _putStr() *removes* a key whose
     * value is empty, and Preferences::getString()/getBytes() leave the
     * destination untouched when the key is absent — so without the reset,
     * clearing the hostname, saving, then loading into the same object would
     * hand the old hostname straight back, and the store would read as agreeing
     * with it. The numeric fields get this from getUInt()'s default argument;
     * the string and byte fields have no such argument and need the explicit
     * reset. A subclass override must do the same for its own fields.
     *
     * @param p Open, read-only Preferences object.
     * @return true once the fields have been read.
     */
    virtual bool _doLoad(Preferences& p) {
        _dhcp     = p.getBool( "dhcp", true);
        if (!_dhcp) {
            // load persisted static config
            _ip       = IPAddress(p.getUInt("ip",   0));
            _mask     = IPAddress(p.getUInt("mask", 0));
            _gateway  = IPAddress(p.getUInt("gw",   0));
            for (uint8_t i = 0; i < DNS_SERVER_COUNT; i++) {
                char key[8];
                snprintf(key, sizeof(key), "dns%u", i);
                _dns[i] = IPAddress(p.getUInt(key, 0));
            }
        } else {
            _ip       = IPAddress();
            _mask     = IPAddress();
            _gateway  = IPAddress();
            for (uint8_t i = 0; i < DNS_SERVER_COUNT; i++) _dns[i] = IPAddress();
        }
        _priority = p.getUChar("prio", 0);
        _hostname[0] = '\0';                    // absent key => generated default
        prefs_getStr(p, "host", _hostname, MAX_HOSTNAME_SIZE);
        memset(_mac, 0, MAC_LEN);               // absent key => _macDefault, since
        prefs_getBytes(p, "mac",  _mac, MAC_LEN);     // an all-zero _mac is not valid

        return true;
    }

    // ------------------------------------------------------------------------
    // Atomic bulk configuration helpers — call only while holding _mutex
    // ------------------------------------------------------------------------

    /**
     * @brief Copies the configuration fields from @p cfg into the profile.
     *
     * Does not validate the incoming values. Called by setConfig() and
     * subclass setConfig() overrides inside a single mutex acquisition
     * to ensure atomicity across base and subclass fields.
     *
     * @param cfg Source configuration struct.
     */
    void _doSetConfig(const NetworkConfig& cfg) {
        _dhcp    = cfg.dhcp;
        _ip      = cfg.ip;
        _mask    = cfg.mask;
        _gateway = cfg.gateway;
        for (uint8_t i = 0; i < DNS_SERVER_COUNT; i++) _dns[i] = cfg.dns[i];
        strncpy(_hostname, cfg.hostname, MAX_HOSTNAME_LEN);
        _hostname[MAX_HOSTNAME_LEN] = '\0';
        _priority = cfg.priority;
        memcpy(_mac, cfg.mac, MAC_LEN);
    }

    /**
     * @brief Copies the configuration fields from the profile into @p cfg.
     *
     * Called by getConfig() and subclass getConfig() overrides inside a
     * single mutex acquisition to ensure a consistent snapshot.
     *
     * @param cfg Destination configuration struct.
     */
    void _doGetConfig(NetworkConfig& cfg) const {
        cfg.dhcp     = _dhcp;
        cfg.ip       = _ip;
        cfg.mask     = _mask;
        cfg.gateway  = _gateway;
        for (uint8_t i = 0; i < DNS_SERVER_COUNT; i++) cfg.dns[i] = _dns[i];
        strncpy(cfg.hostname, _hostname, MAX_HOSTNAME_SIZE);
        cfg.priority = _priority;
        memcpy(cfg.mac, _mac, MAC_LEN);
    }


    // -----------------------------------------------------------------------
    // Serialization hooks and helpers
    // -----------------------------------------------------------------------

    /**
     * @brief Appends interface-specific fields to an in-progress JSON buffer.
     *
     * Called by toJson() while the mutex is held. Subclasses must prepend
     * a comma before each new field and must NOT append the closing '}'.
     * Default implementation is a no-op.
     *
     * @param json JSON buffer being built.
     * @param len  Total capacity of @p json in bytes.
     * @return true if all content fit, false on truncation.
     */
    virtual bool _doJson(char* /*json*/, size_t /*len*/) const {
        return true;
    }

    /**
     * @brief Appends @p buffer to @p json without exceeding @p jsonLen bytes.
     *
     * Thin wrapper over json_cat() (detail/json_tools.h), kept so subclasses and
     * existing call sites are unaffected by the helpers moving to a header
     * shared with NetworkManager.
     *
     * @param json     Destination JSON string in RAM (null-terminated).
     * @param buffer   String to append, in RAM.
     * @param jsonLen  Total capacity of @p json in bytes.
     * @return @c true on success, @c false if the output was truncated.
     */
    static bool _strncatJson(char* json, const char* buffer, size_t jsonLen) {
        return json_cat(json, buffer, jsonLen);
    }

    /**
     * @brief Program-space counterpart of _strncatJson().
     *
     * @param json     Destination JSON string in RAM (null-terminated).
     * @param buffer   String to append, in program space (PSTR/PROGMEM).
     * @param jsonLen  Total capacity of @p json in bytes.
     * @return @c true on success, @c false if the output was truncated.
     */
    static bool _strncatJson_P(char* json, PGM_P buffer, size_t jsonLen) {
        return json_cat_P(json, buffer, jsonLen);
    }

    /**
     * @brief Did an snprintf()-family call write the whole string?
     *
     * Thin wrapper over json_fitted() (detail/json_tools.h).
     *
     * @param written Return value of the snprintf() call.
     * @param bufSize Size of the destination buffer.
     * @return @c true if the whole string fit.
     */
    static bool _fitted(int written, size_t bufSize) {
        return json_fitted(written, bufSize);
    }

    /**
     * @brief Escapes a string for safe inclusion in a JSON value.
     *
     * Printable ASCII characters (0x20–0x7E) are copied as-is, with the
     * exception of '"' and '\' which are escaped as '\"' and '\\'.
     * All other characters (non-printable, extended ASCII) are escaped
     * as \uXXXX (RFC 8259 compliant Unicode escape sequences).
     *
     * @param dst    Destination buffer.
     * @param dstLen Size of destination buffer in bytes.
     * @param src    Null-terminated source string.
     * @return Number of bytes written to @p dst, excluding null terminator.
     */
    static size_t _jsonEscapeStr(char* dst, size_t dstLen, const char* src) {
        return json_escape(dst, dstLen, src);   // detail/json_tools.h
    }

    // ------------------------------------------------------------------------
    // MAC default storage — written by _initMacDefault(), and once more by
    // setFactoryMac() where that could not read it; read-only after that
    // ------------------------------------------------------------------------
    uint8_t _macDefault[MAC_LEN];
    /// Whether _macDefault holds the interface's real factory address. Set by
    /// _initMacDefault(), or later and once by setFactoryMac().
    bool    _macDefaultKnown;
    char    _hostnameDefault[MAX_HOSTNAME_SIZE];

    // ------------------------------------------------------------------------
    // Thread safety
    // ------------------------------------------------------------------------

    // Lock: a real timed mutex exists only where there is preemptive
    // concurrency (ESP32, and the host-test build). AVR is single-threaded and
    // ESP8266 schedules its callbacks cooperatively (never preempting loop()),
    // so on both the lock is a no-op.
#if !defined(ARDUINO_ARCH_AVR) && !defined(ARDUINO_ARCH_ESP8266)
    mutable std::timed_mutex _mutex;
    bool _lock(uint32_t timeoutMs = MUTEX_TIMEOUT) const {
        return _mutex.try_lock_for(std::chrono::milliseconds(timeoutMs));
    }
    void _unlock() const { _mutex.unlock(); }
#else
    bool _lock(uint32_t /*timeoutMs*/ = MUTEX_TIMEOUT) const { return true; }
    void _unlock() const {}
#endif

    static constexpr bool _RW_MODE = false;
    static constexpr bool _RO_MODE = true;
};
