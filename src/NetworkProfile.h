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
#include "Host.h"

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
#  include <Preferences.h>
#elif defined(ARDUINO_ARCH_AVR)
#  include <EEPROMPreferences.h>
   using Preferences = EEPROMPreferences; 
#endif

#if defined(ARDUINO_ARCH_AVR)
#  include <avr/boot.h>
#elif !defined(ARDUINO_ARCH_ESP8266)
#  include <mutex>
#  include <chrono>
#endif

#ifndef NETWORK_PROFILE_MUTEX_TIMEOUT
#  define NETWORK_PROFILE_MUTEX_TIMEOUT 1000
#endif

#if defined(ARDUINO_ARCH_AVR)
#  define NETWORK_PROFILE_MAX_DNS_SERVERS 1     // DNSClient limitation
#  define NETWORK_PROFILE_DEFAULT_DNS_SERVERS 1
#  define NETWORK_PROFILE_MAX_NTP_SERVERS 1
#  define NETWORK_PROFILE_DEFAULT_NTP_SERVERS 0 // AVR: opt-in (conserve SRAM)
#elif defined(ARDUINO_ARCH_ESP8266)
#  define NETWORK_PROFILE_MAX_DNS_SERVERS 2
#  define NETWORK_PROFILE_DEFAULT_DNS_SERVERS 2
#  define NETWORK_PROFILE_MAX_NTP_SERVERS 3
#  define NETWORK_PROFILE_DEFAULT_NTP_SERVERS 3
#elif defined(ARDUINO_ARCH_ESP32)
#  define NETWORK_PROFILE_MAX_DNS_SERVERS 3     // LwIP configuration
#  define NETWORK_PROFILE_DEFAULT_DNS_SERVERS 2 // Limited to 2 by WiFi API
#  define NETWORK_PROFILE_MAX_NTP_SERVERS 3
#  define NETWORK_PROFILE_DEFAULT_NTP_SERVERS 3
#endif

#ifndef NETWORK_PROFILE_DNS_SERVER_COUNT
#  define NETWORK_PROFILE_DNS_SERVER_COUNT NETWORK_PROFILE_DEFAULT_DNS_SERVERS
#endif

#if (NETWORK_PROFILE_DNS_SERVER_COUNT < 1) || (NETWORK_PROFILE_DNS_SERVER_COUNT > NETWORK_PROFILE_MAX_DNS_SERVERS)
#  error "NETWORK_PROFILE_DNS_SERVER_COUNT must be in [1-NETWORK_PROFILE_MAX_DNS_SERVERS] range."
#endif

#ifndef NETWORK_PROFILE_NTP_SERVER_COUNT
#  define NETWORK_PROFILE_NTP_SERVER_COUNT NETWORK_PROFILE_DEFAULT_NTP_SERVERS
#endif

#if (NETWORK_PROFILE_NTP_SERVER_COUNT < 0) || (NETWORK_PROFILE_NTP_SERVER_COUNT > NETWORK_PROFILE_MAX_NTP_SERVERS)
#  error "NETWORK_PROFILE_NTP_SERVER_COUNT must be in [1-NETWORK_PROFILE_MAX_NTP_SERVERS] range."
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
 * gateway, DNS servers), device-level settings (hostname, NTP servers,
 * interface priority, MAC address), and provides thread-safe load/save
 * via Preferences on ESP platforms.
 *
 * Subclasses extend _doSave(), _doLoad(), _doJson() and _initMacDefault()
 * to handle interface-specific fields (e.g. WiFi credentials, ETH MAC type).
 *
 * Platform support:
 *   - ESP32:   std::timed_mutex
 *   - ESP8266: cooperative scheduling, _lock()/_unlock() are no-ops
 *   - AVR:     single-threaded, _lock()/_unlock() are no-ops;
 *              saveCfg()/loadCfg() delegate to user-supplied callbacks
 */
class NetworkProfile {
public:
    /** @brief Number of NTP servers stored per profile. */
    static constexpr uint8_t  NTP_SERVER_COUNT  = NETWORK_PROFILE_NTP_SERVER_COUNT;

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
     * Sized to fit the worst case output of toJson(), including:
     * - Three NTP FQDNs of 253 characters each (~780 bytes)
     * - 63-character hostname
     * - All IP fields in dotted-decimal format
     * - MAC address ("AA:BB:CC:DD:EE:FF")
     * - WiFiProfile SSID worst case: 32 bytes fully escaped → 192 chars
     * Total worst case: ~1150 bytes; 1200 provides a safety margin.
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

#if defined(ARDUINO_ARCH_AVR)
    /**
     * @brief Callback type for saving profile data on AVR platforms.
     * @param profile Reference to the profile to save.
     */
    using SaveCallback = void (*)(const NetworkProfile&);

    /**
     * @brief Callback type for loading profile data on AVR platforms.
     * @param profile Reference to the profile to load into.
     */
    using LoadCallback = void (*)(NetworkProfile&);
#endif

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
     * @note The ntp[] array holds string representations (dotted-decimal IP or
     *       FQDN). Each entry is validated (IP or FQDN) and stored by
     *       setConfig().
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
#if (NETWORK_PROFILE_NTP_SERVER_COUNT > 0)
        char        ntp[NTP_SERVER_COUNT][Host::MAX_FQDN_SIZE] = {};
#endif
        uint8_t     priority = 0;
        MACAddress  mac      = {};
    };

    // ------------------------------------------------------------------------
    // Constructors
    // ------------------------------------------------------------------------

    /**
     * @brief Constructs a NetworkProfile with default values.
     *
     * DHCP is enabled, all IP fields are 0.0.0.0, hostname and NTP
     * servers are empty, priority is 0. The default MAC is generated
     * by _initMacDefault() which is called from the subclass constructor.
     */
    NetworkProfile()
        : _dhcp(true)
        , _ip(IPAddress(0, 0, 0, 0))
        , _mask(IPAddress(0, 0, 0, 0))
        , _gateway(IPAddress(0, 0, 0, 0))
        , _dns{}
        , _hostname{}
    #if (NETWORK_PROFILE_NTP_SERVER_COUNT > 0)
        , _ntp{}
    #endif
        , _priority(0)
        , _mac{}
        , _macDefault{}
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

#if (NETWORK_PROFILE_NTP_SERVER_COUNT > 0)
    /**
     * @brief Copies the configured NTP server (IP or FQDN) at @p index into @p out.
     *
     * Read under the profile's own mutex, so it composes atomically with the
     * other getters (getConfig() captures NTP in the same lock).
     *
     * @param index Server index in [0, NTP_SERVER_COUNT).
     * @param out   Destination buffer; set to "" on any failure.
     * @param len   Size of @p out in bytes.
     * @return true if the value (possibly empty) fit; false on invalid index,
     *         truncation, or mutex timeout.
     */
    bool getNtp(uint8_t index, char* out, size_t len) const {
        if (!out || len == 0) return false;
        if (index >= NTP_SERVER_COUNT) { out[0] = '\0'; return false; }
        if (!_lock()) { out[0] = '\0'; return false; }
        int w = snprintf(out, len, "%s", _ntp[index]);
        _unlock();
        return (w >= 0 && (size_t)w < len);
    }

    /**
     * @brief Sets an NTP server by string (IP address or FQDN).
     *
     * The string is validated as a dotted-decimal IP or an FQDN and stored.
     * An empty string clears the slot; an invalid string leaves the entry
     * unchanged.
     * 
     * @param str   Null-terminated IP address or FQDN string.
     * @param index Index in [0, NTP_SERVER_COUNT). Clamped to 0 if out of
     *              range. Defaults to 0.
     * @return true on success, false if @p str is invalid.
     */
    bool setNtp(const char* str, uint8_t index = 0) {
        if (index >= NTP_SERVER_COUNT) index = 0;
        if (!str) return false;
        // Empty clears the slot; otherwise require a dotted-decimal IP or an FQDN.
        if (str[0] != '\0'
            && !Host::isValidIp(str)
            && !Host::isValidFqdn(str)) return false;
        if (!_lock()) return false;
        snprintf(_ntp[index], Host::MAX_FQDN_SIZE, "%s", str);
        _unlock();
        return true;
    }
 
    /**
     * @brief Returns true if at least one NTP server is configured in the
     *        profile.
     *
     * Used by adapters to decide whether to request NTP server addresses via
     * DHCP option 42 (when no explicit server is configured) or to rely on
     * the explicitly configured server(s) instead.
     *
     * @return true if any NTP slot holds a non-empty entry, false if all are
     *         empty or NETWORK_PROFILE_NTP_SERVER_COUNT is 0.
     */
    bool isConfiguredNtp() const {
        if (!_lock()) return false;
        bool configured = false;
        for (uint8_t i = 0; i < NTP_SERVER_COUNT; i++) {
            if (_ntp[i][0] != '\0') { configured = true; break; }
        }
        _unlock();
        return configured;
    }
#endif

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
     * (non-zero), it is returned. If the active MAC is all zeros, the
     * generated default MAC is returned instead (ACTIVE with fallback).
     * If @p source is ConfigSource::FACTORY, the generated MAC is always
     * returned.
     *
     * @param mac    Destination buffer (MAC_LEN bytes).
     * @param source ConfigSource::ACTIVE (default) or ConfigSource::FACTORY.
     * @return true on success, false on mutex timeout.
     */
    bool getMac(MACAddress mac, ConfigSource source = ConfigSource::ACTIVE) const {
        if (!_lock()) return false;

        const uint8_t* src = _mac;
        if (source == ConfigSource::FACTORY || !isValidMac(_mac)) {
            src = _macDefault;
        }

        memcpy(mac, src, MAC_LEN);
        _unlock();
        return true;
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
        snprintf(buf, len, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"),
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
     * @brief Checks whether a MAC address is valid (non-zero, non-broadcast).
     *
     * Returns false if all bytes are 0x00 (unset) or all bytes are 0xFF
     * (broadcast address).
     *
     * @param mac MAC address to validate.
     * @return true if valid, false otherwise.
     */
    static bool isValidMac(const MACAddress mac) {
        bool allZero = true;
        bool allFF   = true;
        for (uint8_t i = 0; i < MAC_LEN; i++) {
            if (mac[i] != 0x00) allZero = false;
            if (mac[i] != 0xFF) allFF   = false;
        }
        return !allZero && !allFF;
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
        BadNtp,             ///< an NTP entry is neither a usable unicast IP nor an FQDN
        BadSsid,            ///< (WiFiProfile) ssid is empty or too long
        BadPassword,        ///< (WiFiProfile) password fails the WPA length rules
        BadTxPower,         ///< (WiFiProfile) txPower is not an allowed level
    };

private:
    /// The address as a plain 32-bit value, most-significant octet first. Not
    /// IPAddress::operator uint32_t(), which is endianness-dependent and would
    /// make a valid mask look non-contiguous on a little-endian MCU.
    static uint32_t ipU32(const IPAddress& x) {
        return ((uint32_t)x[0] << 24) | ((uint32_t)x[1] << 16)
             | ((uint32_t)x[2] <<  8) |  (uint32_t)x[3];
    }

    /// Whether @p a could be a real unicast host/DNS/gateway/NTP address on an
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
     * All base fields (IP, DNS, hostname, NTP, priority, MAC) are updated in a
     * single mutex acquisition.
     *
     * @param cfg Source configuration to apply.
     * @return true on success, false on mutex timeout.
     */
    bool setConfig(const NetworkConfig& cfg) {
        if (!isValidConfig(cfg)) return false;
        if (!_lock()) return false;
        _doSetConfig(cfg);
        _doSetNtpConfig(cfg);
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
        _doGetNtpConfig(cfg);
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
#if (NETWORK_PROFILE_NTP_SERVER_COUNT > 0)
        // An NTP entry, if set, is either a usable unicast IP or an FQDN. The
        // ip parse is String-free (Host::parseIp fills octets).
        for (uint8_t i = 0; i < NTP_SERVER_COUNT; i++) {
            if (cfg.ntp[i][0] == '\0') continue;
            uint8_t oct[4];
            if (Host::parseIp(cfg.ntp[i], oct)) {
                const uint32_t u = ((uint32_t)oct[0] << 24) | ((uint32_t)oct[1] << 16)
                                 | ((uint32_t)oct[2] <<  8) |  (uint32_t)oct[3];
                if (!isUnicastHost(u)) return ConfigCheck::BadNtp;
            } else if (!Host::isValidFqdn(cfg.ntp[i])) {
                return ConfigCheck::BadNtp;
            }
        }
#endif
        return ConfigCheck::Ok;
    }

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
     * [, ntp[0, NTP_SERVER_COUNT) (dotted-decimal or FQDN)].
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
        bool ok = true;

        json[0] = '\0';
        if (!_lock()) return false;
        ok = ok && _strncatJson(json, PSTR("{"), len);
        // DHCP
        snprintf(buf, BS, PSTR("\"dhcp\":%u"), (unsigned)_dhcp);
        ok = ok && _strncatJson(json, buf, len);
        if (!_dhcp) {
            // IP address
            snprintf(buf, BS, PSTR(",\"ip\":\"%d.%d.%d.%d\""),
                _ip[0], _ip[1], _ip[2], _ip[3]);
            ok = ok && _strncatJson(json, buf, len);
            // Netmask
            snprintf(buf, BS, PSTR(",\"mask\":\"%d.%d.%d.%d\""),
                _mask[0], _mask[1], _mask[2], _mask[3]);
            ok = ok && _strncatJson(json, buf, len);
            // Default gateway
            snprintf(buf, BS, PSTR(",\"gw\":\"%d.%d.%d.%d\""),
                _gateway[0], _gateway[1], _gateway[2], _gateway[3]);
            ok = ok && _strncatJson(json, buf, len);
            // DNS servers
            for (uint8_t i = 0; i < DNS_SERVER_COUNT; i++) {
                snprintf(buf, BS, PSTR(",\"dns%u\":\"%d.%d.%d.%d\""),
                    i, _dns[i][0], _dns[i][1], _dns[i][2], _dns[i][3]);
                ok = ok && _strncatJson(json, buf, len);
            }
        }
        // Hostname
        snprintf(buf, BS, PSTR(",\"host\":\"%s\""),
            _hostname[0] != '\0' ? _hostname : _hostnameDefault);
        ok = ok && _strncatJson(json, buf, len);
        // Interface priority
        snprintf(buf, BS, PSTR(",\"prio\":%u"), (unsigned)_priority);
        ok = ok && _strncatJson(json, buf, len);

        // Active MAC — override if valid, otherwise default
        const uint8_t* macSrc = isValidMac(_mac) ? _mac : _macDefault;
        macToStr(macBuf, sizeof(macBuf), macSrc);
        snprintf(buf, BS, PSTR(",\"mac\":\"%s\""), macBuf);
        ok = ok && _strncatJson(json, buf, len);

#if (NETWORK_PROFILE_NTP_SERVER_COUNT > 0)
        for (uint8_t i = 0; i < NTP_SERVER_COUNT; i++) {
            char ntpKey[Host::MAX_FQDN_SIZE + 12];
            snprintf(ntpKey, sizeof(ntpKey), PSTR(",\"ntp%u\":\"%s\""), i, _ntp[i]);
            ok = ok && _strncatJson(json, ntpKey, len);
        }
#endif

        // JSON serialization hook for childs
        ok = ok && _doJson(json, len);
        ok = ok && _strncatJson(json, PSTR("}"), len);

        _unlock();

        // Empty string instead of a malformed JSON stucture.
        if (!ok) json[0] = '\0';
        return ok;
    }

    // ------------------------------------------------------------------------
    // Persistence — ESP platforms only; AVR uses callbacks
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
     * On ESP32/ESP8266: erases the entire Preferences namespace @p ns.
     * After calling this, a reboot will load default values.
     * On AVR: no-op, returns false (application responsibility).
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
#if (NETWORK_PROFILE_NTP_SERVER_COUNT > 0)
    char          _ntp[NTP_SERVER_COUNT][Host::MAX_FQDN_SIZE];
#endif
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
     * Also generates _hostnameDefault from the last 3 bytes of _macDefault,
     * e.g. "ESP-02D6FC" or "AVR-4E5230".
     */
    virtual void _initMacDefault() = 0;

    /**
     * @brief Generates _hostnameDefault from _macDefault.
     *
     * Called by subclasses at the end of _initMacDefault(), after _macDefault
     * has been populated. Format: "<PREFIX>-XXYYZZ" where XX, YY, ZZ are the
     * last three bytes of _macDefault in uppercase hex.
     *
     * @param prefix Platform prefix string (e.g. "ESP", "AVR").
     */
    void _generateHostnameDefault(const char* prefix) {
        snprintf(_hostnameDefault, MAX_HOSTNAME_SIZE, "%s-%02X%02X%02X",
            prefix,
            _macDefault[3], _macDefault[4], _macDefault[5]);
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
        if (value[0] != '\0') return p.putString(key, value) > 0;
        p.remove(key);
        return true;
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
            p.remove( "ip");
            p.remove( "mask");
            p.remove( "gw");
        }
        for (uint8_t i = 0; i < DNS_SERVER_COUNT; i++) {
            char key[8];
            snprintf(key, sizeof(key), "dns%u", i);
            if (!_dhcp)
                ok = ok && (p.putUInt(key, (uint32_t)_dns[i]) > 0);
            else
                p.remove(key);
        }
        ok = ok && (_putStr(p, "host", _hostname)         > 0);
        ok = ok && (p.putUChar( "prio", _priority)         > 0);
        ok = ok && (p.putBytes( "mac",  _mac, MAC_LEN) == MAC_LEN);

#if (NETWORK_PROFILE_NTP_SERVER_COUNT > 0)
        for (uint8_t i = 0; i < NTP_SERVER_COUNT; i++) {
            char key[8];
            snprintf(key, sizeof(key), "ntp%u", i);
            ok = ok && (_putStr(p, key, _ntp[i])          > 0);
        }
#endif
        return ok;
    }

    /**
     * @brief Loads profile-specific fields from an already-open Preferences
     *        object.
     *
     * The namespace-presence and completed-transaction checks (the "_ok"
     * marker) are handled by loadCfg() before this runs; a subclass override
     * only needs to read its own fields.
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
        p.getString("host", _hostname, MAX_HOSTNAME_SIZE);
        p.getBytes( "mac",  _mac, MAC_LEN);

#if (NETWORK_PROFILE_NTP_SERVER_COUNT > 0)
        for (uint8_t i = 0; i < NTP_SERVER_COUNT; i++) {
            char key[8];
            snprintf(key, sizeof(key), "ntp%u", i);
            _ntp[i][0] = '\0';
            p.getString(key, _ntp[i], Host::MAX_FQDN_SIZE);
        }
#endif
        return true;
    }

    // ------------------------------------------------------------------------
    // Atomic bulk configuration helpers — call only while holding _mutex
    // ------------------------------------------------------------------------

    /**
     * @brief Copies all non-NTP fields from @p cfg into the profile.
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
     * @brief Copies NTP server strings from @p cfg into the profile.
     *
     * Call while holding _mutex.
     * An empty string resets the corresponding NTP slot.
     *
     * @param cfg Source configuration struct.
     */
    void _doSetNtpConfig(const NetworkConfig& cfg) {
#if (NETWORK_PROFILE_NTP_SERVER_COUNT > 0)
        for (uint8_t i = 0; i < NTP_SERVER_COUNT; i++)
            snprintf(_ntp[i], Host::MAX_FQDN_SIZE, "%s", cfg.ntp[i]);
#else
        (void)cfg;   // NTP disabled at compile time: nothing to copy
#endif
    }

    /**
     * @brief Copies all non-NTP fields from the profile into @p cfg.
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

    /**
     * @brief Copies NTP server strings from the profile into @p cfg.
     *
     * Call while holding _mutex.
     *
     * @param cfg Destination configuration struct.
     */
    void _doGetNtpConfig(NetworkConfig& cfg) const {
#if (NETWORK_PROFILE_NTP_SERVER_COUNT > 0)
        for (uint8_t i = 0; i < NTP_SERVER_COUNT; i++)
            snprintf(cfg.ntp[i], Host::MAX_FQDN_SIZE, "%s", _ntp[i]);
#else
        (void)cfg;   // NTP disabled at compile time: nothing to fill
#endif
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
     * @return true on success, false if output was truncated.
     */
    static bool _strncatJson(char* json, const char* buffer, size_t jsonLen) {
        size_t remaining = (strlen(json) < jsonLen)
                        ? (jsonLen - strlen(json) - 1)
                        : 0;
        if (remaining == 0) return false;
        size_t toAppend = strlen(buffer);
        strncat(json, buffer, remaining);
        return toAppend <= remaining;
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
        if (!dst || !src || dstLen == 0) return 0;
        size_t written = 0;
        while (*src && written + 1 < dstLen) {
            unsigned char c = (unsigned char)*src++;
            if (c >= 0x20 && c <= 0x7E && c != '"' && c != '\\') {
                // Printable ASCII — copy directly
                dst[written++] = c;
            } else {
                // Non-printable or JSON special character — \uXXXX escape
                if (written + 6 >= dstLen) break;
                written += snprintf(dst + written, dstLen - written,
                                    "\\u%04X", c);
            }
        }
        dst[written] = '\0';
        return written;
    }

    // ------------------------------------------------------------------------
    // MAC default storage — written by _initMacDefault(), read-only after that
    // ------------------------------------------------------------------------
    uint8_t _macDefault[MAC_LEN];
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