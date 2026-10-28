/*
 * Header-only: the Arduino IDE compiles libraries separately from the sketch,
 * so a precompiled unit would not see the configuration macros you define —
 * being header-only, the library is compiled with each includer's macros
 * instead. In a multi-file project, define those macros globally so every
 * translation unit agrees (see the README).
 */

#pragma once

#if !defined(ARDUINO_ARCH_AVR)
#include "NetworkProfile.h"

#if defined(ARDUINO_ARCH_ESP32)
#  include <esp_mac.h>
#  include <soc/soc_caps.h>   // SOC_WIFI_SUPPORTED
#  include <WiFi.h>
#elif defined(ARDUINO_ARCH_ESP8266)
#  include <ESP8266WiFi.h>
#else
#  error "WiFiProfile requires ESP32 or ESP8266. (On AVR the whole class is " \
         "compiled out by the guard above, so this is a genuinely unsupported " \
         "architecture — RP2040, STM32 and the like.)"
#endif

#ifndef WIFI_PROFILE_DEFAULT_WIFI_TX_POWER
#  if defined(ARDUINO_ARCH_ESP32)
#    define     WIFI_PROFILE_DEFAULT_WIFI_TX_POWER 19.5f
#  elif defined(ARDUINO_ARCH_ESP8266)
#    define     WIFI_PROFILE_DEFAULT_WIFI_TX_POWER 20.5f
#  endif
#endif

// MAX_SSID_LEN is exposed as a macro (not a class constant) for a platform
// reason: the ESP32 WiFi SDK already defines MAX_SSID_LEN as a macro (= 32),
// which collides with any same-named class constant. Defining it as a guarded
// macro here keeps the symbol uniform across ESP32, ESP8266 and AVR — the
// value is the IEEE 802.11 SSID limit (32) on every platform. Consequence: use
// the unqualified MAX_SSID_LEN (a macro cannot be written as
// WiFiProfile::MAX_SSID_LEN). MAX_PASSWORD_LEN has no such SDK collision and
// stays a class constant.
#ifndef MAX_SSID_LEN
#  define MAX_SSID_LEN 32
#endif

/**
 * @brief Network profile for WiFi interfaces.
 *
 * Extends NetworkProfile with WiFi-specific credentials (SSID and password).
 * Provides atomic get/set via WiFiCredentials and WiFiConfig structs,
 * consistent with the NetworkConfig pattern in the base class.
 *
 * Persistence:
 *   - On ESP32/ESP8266: credentials are saved/loaded alongside the base
 *     IP configuration via _doSave() / _doLoad() overrides.
 *
 * @note SSID max length is 32 bytes (IEEE 802.11); password max length
 *       is 63 bytes (WPA2-PSK). Both are stored as null-terminated strings.
 */
class WiFiProfile : public NetworkProfile {
public:

    /**
     * @brief Selects between the supported Wi-Fi security configurations.
     *
     * These are not actual Wi-Fi security protocols; they merely describe the
     * basis for authentication.
     */
    enum class WiFiSecurity : uint8_t {
        UNKNOWN  = 0,
        OPEN     = 1,
        PASSWORD = 2,
    };


    /** 
     * @brief Maximum SSID length in bytes, excluding null terminator.
     * Exposed as the macro MAX_SSID_LEN (= 32); see the note at the top of
     * this file for why it is a macro rather than a class constant.
     */
    // static constexpr size_t MAX_SSID_LEN     = 32;

    /** @brief Buffer size for SSID including null terminator. */
    static constexpr size_t MAX_SSID_SIZE     = MAX_SSID_LEN + 1;

    /** 
     * @brief Maximum WPA2-PSK passphrase length in bytes, excluding null
     *        terminator.
     */
    static constexpr size_t MAX_PASSWORD_LEN  = 63;

    /** @brief Buffer size for password including null terminator. */
    static constexpr size_t MAX_PASSWORD_SIZE = MAX_PASSWORD_LEN + 1;

#ifdef ARDUINO_ARCH_ESP32
    /** @brief Minimal WiFi trasmit power in dBm */
    static constexpr float MIN_WIFI_TX_POWER_dBm  = -1.0f;
    /** @brief Maximal WiFi trasmit power in dBm */
    static constexpr float MAX_WIFI_TX_POWER_dBm  = 19.5f;
    /** @brief WiFi trasmit power step in dBm */
    static constexpr float WIFI_TX_POWER_STEP_dBm = 0.25f;

    /**
     * @brief Supported transmit power levels in the hardware's own unit
     *        (quarter dBm), ascending.
     *
     * Kept as integers because that is what both the enum and the radio use;
     * dBm is a presentation of this, not the other way round. Comparing in the
     * native unit also removes every float equality test — the current
     * "multiply by four and check for a whole number" trick is exact only
     * because 0.25 happens to be a power of two, which is not a property
     * anyone should have to rediscover.
     */
    static constexpr int16_t WIFI_TX_POWER_LEVELS_Q[] = {
        WIFI_POWER_MINUS_1dBm, WIFI_POWER_2dBm,   WIFI_POWER_5dBm,
        WIFI_POWER_7dBm,       WIFI_POWER_8_5dBm, WIFI_POWER_11dBm,
        WIFI_POWER_13dBm,      WIFI_POWER_15dBm,  WIFI_POWER_17dBm,
        WIFI_POWER_18_5dBm,    WIFI_POWER_19dBm,  WIFI_POWER_19_5dBm,
    };
    static constexpr uint8_t WIFI_TX_POWER_LEVEL_COUNT =
        sizeof(WIFI_TX_POWER_LEVELS_Q) / sizeof(WIFI_TX_POWER_LEVELS_Q[0]);

#elif defined(ARDUINO_ARCH_ESP8266)
    /** @brief Minimal WiFi trasmit power in dBm */
    static constexpr float MIN_WIFI_TX_POWER_dBm  =  0.0f;
    /** @brief Maximal WiFi trasmit power in dBm */
    static constexpr float MAX_WIFI_TX_POWER_dBm  = 20.5f;
    /** @brief WiFi trasmit power step in dBm */
    static constexpr float WIFI_TX_POWER_STEP_dBm = 0.25f;
#endif

    /** @brief Default WiFi transmit power in dBm */
    static constexpr float DEFAULT_WIFI_TX_POWER_dBm = WIFI_PROFILE_DEFAULT_WIFI_TX_POWER;

    /**
     * @brief Multiplier for converting dBm to the platform's integer TX power
     *        unit.
     *
     * Derived from WIFI_TX_POWER_STEP_dBm to avoid runtime division.
     * Used for validation in isValidTxPower().
     */
    static constexpr float WIFI_TX_POWER_MULTIPLIER = 1.0f / WIFI_TX_POWER_STEP_dBm;

    /**
     * @brief Aggregates WiFi credential fields for atomic get/set.
     *
     * Used with setWiFiCredentials() / getWiFiCredentials() to update or
     * retrieve SSID and password atomically in a single mutex acquisition.
     * Prefer this over individual setters when both fields change at once
     * (e.g. when applying credentials received over MQTT or a web interface).
     */
    struct WiFiCredentials {
        char ssid    [MAX_SSID_SIZE]     = {};
        char password[MAX_PASSWORD_SIZE] = {};
    };

    /**
     * @brief Aggregates all WiFi profile fields for atomic get/set.
     *
     * Extends NetworkConfig with WiFi-specific credential fields.
     * Used with setConfig(WiFiConfig) / getConfig(WiFiConfig) to update or
     * retrieve the complete WiFi profile atomically.
     *
     * Prefer this over NetworkConfig when credentials and IP configuration
     * arrive together (e.g. from a web interface or MQTT message).
     */
    struct WiFiConfig : public NetworkConfig {
        char  ssid    [MAX_SSID_SIZE]     = {};
        char  password[MAX_PASSWORD_SIZE] = {};
        float txPower                     = DEFAULT_WIFI_TX_POWER_dBm;
    };

    // ------------------------------------------------------------------------
    // Constructors
    // ------------------------------------------------------------------------

    /**
     * @brief Constructs a WiFiProfile with default values.
     *
     * SSID and password are empty strings; base fields are default.
     */
    WiFiProfile()
        : NetworkProfile()
        , _ssid{}
        , _password{}
        , _txPower(DEFAULT_WIFI_TX_POWER_dBm)
    {
        _initMacDefault();
    }

    /**
     * @brief Constructs a WiFiProfile from a NetworkConfig struct.
     *
     * Sets base IP configuration only; SSID and password remain empty.
     * Use WiFiConfig constructor to set all fields at once.
     *
     * @param cfg Initial base configuration to apply.
     */
    explicit WiFiProfile(const NetworkConfig& cfg)
        : NetworkProfile(cfg)
        , _ssid{}
        , _password{}
        , _txPower(DEFAULT_WIFI_TX_POWER_dBm)
    {
        _initMacDefault();
    }

    /**
     * @brief Constructs a WiFiProfile from a WiFiConfig struct.
     *
     * Sets all fields including SSID, password and transmit power. Each is
     * validated before storing, and an invalid one is silently ignored — the
     * member keeps its default rather than the constructor failing, since a
     * constructor has no way to report it:
     * - If the SSID is invalid, it is silently ignored and _ssid remains
     *   empty.
     * - If the password is invalid, it is silently ignored and _password
     *   remains empty (equivalent to an open network).
     * - If the transmit power is invalid, it is silently ignored and _txPower
     *   remains DEFAULT_WIFI_TX_POWER_dBm.
     *
     * This is deliberately more forgiving than setConfig(), which rejects the
     * whole struct and returns false if any field fails. Where knowing about a
     * rejection matters, construct empty and call setConfig(), or check with
     * checkConfig() first.
     *
     * Base IP configuration fields are always applied regardless of
     * credential validity.
     *
     * @param cfg Initial full WiFi configuration to apply.
     */
    explicit WiFiProfile(const WiFiConfig& cfg)
        : NetworkProfile(cfg)
        , _ssid{}
        , _password{}
        , _txPower(DEFAULT_WIFI_TX_POWER_dBm)
    {
        _initMacDefault();
        // Validate and store SSID — if invalid, _ssid remains empty
        if (isValidSsid(cfg.ssid)) {
            strncpy(_ssid, cfg.ssid, MAX_SSID_LEN);
            _ssid[MAX_SSID_LEN] = '\0';
        }
        // Validate and store password — if invalid, _password remains empty
        // (empty password = open network, which is a valid fallback)
        if (isValidPassword(cfg.password)) {
            strncpy(_password, cfg.password, MAX_PASSWORD_LEN);
            _password[MAX_PASSWORD_LEN] = '\0';
        }
        // Validate and store transmit power — if invalid, _txPower keeps the
        // platform default. Omitting this left the constructor silently
        // dropping cfg.txPower while setConfig() applied it, so the same
        // struct produced two different profiles depending on how it arrived.
        if (isValidTxPower(cfg.txPower)) {
            _txPower = cfg.txPower;
        }
    }

    /** @brief Destructor. */
    ~WiFiProfile() override = default;

    /**
     * @brief Returns InterfaceType::WIFI.
     *
     * Compile-time constant — no mutex needed.
     */
    InterfaceType getInterfaceType() const override {
        return InterfaceType::WIFI;
    }

    // ------------------------------------------------------------------------
    // Getters / Setters — all thread-safe
    // ------------------------------------------------------------------------

    /**
     * @brief Copies the SSID into the provided buffer.
     * @param buf Destination buffer.
     * @param len Size of the destination buffer in bytes.
     * @return true if the SSID fit entirely, false on truncation or
     *         mutex failure.
     */
    bool getSsid(char* buf, size_t len) const {
        if (!buf || len == 0) return false;
        if (!_lock()) return false;
        strncpy(buf, _ssid, len);
        buf[len - 1] = '\0';
        bool fit = (strlen(_ssid) < len);
        _unlock();
        return fit;
    }

    /**
     * @brief Sets the SSID after validation.
     * @param ssid Null-terminated SSID string (1–32 bytes).
     * @return true on success, false on invalid SSID or mutex failure.
     */
    bool setSsid(const char* ssid) {
        if (!isValidSsid(ssid)) return false;
        if (!_lock()) return false;
        strncpy(_ssid, ssid, MAX_SSID_LEN);
        _ssid[MAX_SSID_LEN] = '\0';
        _unlock();
        return true;
    }

    /**
     * @brief Copies the password into the provided buffer.
     * @param buf Destination buffer.
     * @param len Size of the destination buffer in bytes.
     * @return true if the password fit entirely, false on truncation or
     *         mutex failure.
     */
    bool getPassword(char* buf, size_t len) const {
        if (!buf || len == 0) return false;
        if (!_lock()) return false;
        strncpy(buf, _password, len);
        buf[len - 1] = '\0';
        bool fit = (strlen(_password) < len);
        _unlock();
        return fit;
    }

    /**
     * @brief Sets the password after validation.
     *
     * An empty string is accepted and treated as an open network (no
     * authentication). Non-empty passwords must be 8–63 printable ASCII
     * characters (0x20–0x7E) per WPA2-PSK (IEEE 802.11i / RFC 2898).
     *
     * @param password Null-terminated password string.
     * @return true on success, false on invalid password or mutex failure.
     */
    bool setPassword(const char* password) {
        if (!isValidPassword(password)) return false;
        if (!_lock()) return false;
        strncpy(_password, password, MAX_PASSWORD_LEN);
        _password[MAX_PASSWORD_LEN] = '\0';
        _unlock();
        return true;
    }

    /**
     * @brief Gets the WiFi authentication type.
     *
     * An empty string is accepted in the password and treated as an open
     * network (no authentication). If the first byte of the password is `\0`,
     * the network is an unsecured open network.
     *
     * Returns the *shape* of the credential, never the credential itself, so a
     * caller can render the right controls without the passphrase leaving the
     * profile. UNKNOWN is a third state on purpose: a mutex failure is not an
     * answer, and folding it into either OPEN or PASSWORD would make one of
     * them a lie.
     *
     * @return WiFiSecurity::UNKNOWN on mutex failure,
     *         WiFiSecurity::OPEN on empty password,
     *         WiFiSecurity::PASSWORD otherwise.
     */
    WiFiSecurity getSecurity() const {
        if (!_lock()) return WiFiSecurity::UNKNOWN;
        WiFiSecurity s = (_password[0] == '\0') ?
                          WiFiSecurity::OPEN : WiFiSecurity::PASSWORD;
        _unlock();
        return s;
    }


    /**
     * @brief Gets the maximum WiFi transmit power.
     * @return The transmit power in dBm or NAN on mutex failure.
     */
    float getTxPower() const {
        if (!_lock()) return NAN;
        float dbm = _txPower;
        _unlock();
        return dbm;
    }

    /**
     * @brief Sets the maximum WiFi transmit power.
     *
     * @param pwr The transmit power in dBm.
     * @return true on success, false on mutex failure.
     */
    bool setTxPower(const float dbm) {
        if (!isValidTxPower(dbm)) return false;
        if (!_lock()) return false;
        _txPower = dbm;
        _unlock();
        return true;
    }

    /**
     * @brief Validates an SSID string against IEEE 802.11 rules.
     *
     * Per IEEE 802.11, an SSID is a binary field and may contain any byte
     * value. This function only enforces the length constraint (1–32 bytes).
     * Character content is not restricted — non-printable characters are
     * valid and will be escaped in JSON output via _jsonEscapeStr().
     *
     * @param ssid Null-terminated string to validate.
     * @return true if length is in [1, MAX_SSID_LEN], false otherwise.
     */
    static bool isValidSsid(const char* ssid) {
        if (!ssid) return false;
        size_t len = strlen(ssid);
        return (len >= 1 && len <= MAX_SSID_LEN);
    }

    /**
     * @brief Validates a WiFi password string against WPA2-PSK rules.
     *
     * Rules enforced:
     * - Empty string is accepted (open network, no authentication).
     * - Non-empty passwords must be 8–63 printable ASCII characters
     *   (0x20–0x7E).
     *
     * @param password Null-terminated string to validate.
     * @return true if valid, false otherwise.
     */
    static bool isValidPassword(const char* password) {
        if (!password) return false;
        size_t len = strlen(password);
        // Empty string = open network, accepted
        if (len == 0) return true;
        // WPA2-PSK: 8-63 printable ASCII characters
        if (len < 8 || len > MAX_PASSWORD_LEN) return false;
        for (size_t i = 0; i < len; i++) {
            if (password[i] < 0x20 || password[i] > 0x7E) return false;
        }
        return true;
    }

    /**
     * @brief Validates provided WiFi transmit power value against the current
     *        platform capabilities.
     *
     * @param dbm The transmit power in dBm to validate.
     * @return true if the provided value is valid, false otherwise.
     *
     * "Valid" means different things per platform, and a caller building a UI
     * needs to know which. Both reject a value off the 0.25 dBm grid rather
     * than snapping it, so a UI should offer steps rather than a free field,
     * and both accept exactly what toJson() can print without rounding.
     *
     * On ESP32 the radio supports a dozen discrete levels and anything between
     * two of them is rejected, so the levels in WIFI_TX_POWER_LEVELS_Q are the
     * only accepted values — being on the grid is necessary but not sufficient.
     *
     * On ESP8266 the whole 0.25 dBm grid within
     * [MIN_WIFI_TX_POWER_dBm, MAX_WIFI_TX_POWER_dBm] is available, so the grid
     * check is the whole test.
     */
    static bool isValidTxPower(const float dbm) {
#ifdef ARDUINO_ARCH_ESP32
        const float q = dbm * WIFI_TX_POWER_MULTIPLIER;      // dBm -> quarter dBm
        const int16_t qi = (int16_t)lroundf(q);
        if (fabsf(q - (float)qi) > 0.001f) return false;     // not on the grid at all
        for (uint8_t i = 0; i < WIFI_TX_POWER_LEVEL_COUNT; i++) {
            if (qi == WIFI_TX_POWER_LEVELS_Q[i]) return true;
        }
        return false;
#elif defined (ARDUINO_ARCH_ESP8266)
        if (dbm < MIN_WIFI_TX_POWER_dBm || dbm > MAX_WIFI_TX_POWER_dBm) return false;
        // On the grid too, by the same "multiply by four and check for a whole
        // number" test the ESP32 branch uses. setOutputPower() truncates to the
        // grid on its way to the radio, so a value between two steps was
        // accepted, stored and reported back as something the radio never
        // produced — and a profile that answers with a figure the hardware is
        // not using is worse than one that refuses it. It also makes the
        // 0.25 dBm granularity that toJson() prints exact rather than rounded.
        const float q = dbm * WIFI_TX_POWER_MULTIPLIER;      // dBm -> quarter dBm
        const int16_t qi = (int16_t)lroundf(q);
        return fabsf(q - (float)qi) <= 0.001f;
#else
        return false;
#endif
    }

    // ------------------------------------------------------------------------
    // Atomic bulk configuration
    // ------------------------------------------------------------------------

    /**
     * @brief Atomically sets SSID and password.
     * @param creds Source credentials to apply.
     * @return true on success, false on invalid credentials or mutex timeout.
     */
    bool setWiFiCredentials(const WiFiCredentials& creds) {
        if (!isValidSsid(creds.ssid))         return false;
        if (!isValidPassword(creds.password)) return false;
        if (!_lock()) return false;
        strncpy(_ssid,     creds.ssid,     MAX_SSID_LEN);
        strncpy(_password, creds.password, MAX_PASSWORD_LEN);
        _ssid[MAX_SSID_LEN]         = '\0';
        _password[MAX_PASSWORD_LEN] = '\0';
        _unlock();
        return true;
    }

    /**
     * @brief Atomically reads SSID and password.
     * @param creds Destination structure to fill.
     * @return true on success, false on mutex timeout.
     */
    bool getWiFiCredentials(WiFiCredentials& creds) const {
        if (!_lock()) return false;
        strncpy(creds.ssid,     _ssid,     MAX_SSID_SIZE);
        strncpy(creds.password, _password, MAX_PASSWORD_SIZE);
        _unlock();
        return true;
    }

    /**
     * @brief Atomically sets all WiFi profile fields from a WiFiConfig struct.
     *
     * Calls NetworkProfile::setConfig() for base fields, then sets SSID
     * and password. SSID and password are validated before storing.
     *
     * @param cfg Source configuration to apply.
     * @return true on success, false on invalid credentials or mutex timeout.
     */
    bool setConfig(const WiFiConfig& cfg) {
        if (!isValidConfig(cfg)) return false;
        if (!_lock()) return false;
        _doSetConfig(cfg);
        strncpy(_ssid,     cfg.ssid,     MAX_SSID_LEN);
        strncpy(_password, cfg.password, MAX_PASSWORD_LEN);
        _ssid[MAX_SSID_LEN]         = '\0';
        _password[MAX_PASSWORD_LEN] = '\0';
        _txPower = cfg.txPower;
        _unlock();
        return true;
    }

    /**
     * @brief Atomically reads all WiFi profile fields into a WiFiConfig
     *        struct.
     *
     * Calls NetworkProfile::getConfig() for base fields, then copies SSID
     * and password.
     *
     * @param cfg Destination structure to fill.
     * @return true on success, false on mutex timeout.
     */
    bool getConfig(WiFiConfig& cfg) const {
        if (!_lock()) return false;
        _doGetConfig(cfg);
        strncpy(cfg.ssid,     _ssid,     MAX_SSID_SIZE);
        strncpy(cfg.password, _password, MAX_PASSWORD_SIZE);
        cfg.txPower = _txPower;
        _unlock();
        return true;
    }

    /**
     * @brief Makes the base-class overloads visible alongside the Wi-Fi ones.
     *
     * Declaring checkConfig(WiFiConfig) and isValidConfig(WiFiConfig) below
     * would otherwise hide their inherited NetworkConfig overloads — C++ name
     * lookup stops at the first scope holding the name, so the parameter types
     * never get to disambiguate across class scopes. These using-declarations
     * bring the base overloads back into scope.
     *
     * They do not make the choice dynamic, though: these are static functions,
     * so a call through a NetworkProfile pointer or reference always selects the
     * base overload at compile time, silently skipping the SSID, password and
     * TX-power checks. Callers holding a base pointer must branch on
     * getInterfaceType() and cast, exactly as they already do for setConfig().
     */
    using NetworkProfile::checkConfig;
    using NetworkProfile::isValidConfig;

    /**
     * @brief Validates a whole WiFi config and names the first problem, or Ok.
     *
     * Adds the WiFi-specific checks (ssid, password, txPower) to the base
     * NetworkProfile::checkConfig(). The detailed counterpart of the bool
     * isValidConfig(WiFiConfig) wrapper below.
     *
     * @param cfg Configuration struct to check.
     * @return ConfigCheck::Ok, or the first problem found.
     */
    static ConfigCheck checkConfig(const WiFiConfig& cfg) {
        if (!isValidSsid(cfg.ssid))         return ConfigCheck::BadSsid;
        if (!isValidPassword(cfg.password)) return ConfigCheck::BadPassword;
        if (!isValidTxPower(cfg.txPower))   return ConfigCheck::BadTxPower;
        // Explicit: only the addressing part is left to check, and the deleted
        // derived-type overload would otherwise (rightly) reject this call.
        return NetworkProfile::checkConfig(static_cast<const NetworkConfig&>(cfg));
    }

    /**
     * @brief Whole WiFi-config validity as a bool.
     *
     * The storability gate used by setConfig(); a thin wrapper over
     * checkConfig(). Called before mutex acquisition.
     *
     * @param cfg Configuration struct to check.
     * @return true if all checks passed, false otherwise.
     */
    static bool isValidConfig(const WiFiConfig& cfg) {
        return checkConfig(cfg) == ConfigCheck::Ok;
    }

protected:
    // ------------------------------------------------------------------------
    // MAC initialisation
    // ------------------------------------------------------------------------

    /**
     * @brief Initialises the default MAC address for the WiFi interface.
     *
     * Platform-specific implementation:
     * - ESP32:   esp_read_mac(_macDefault, ESP_MAC_WIFI_STA)
     * - ESP8266: WiFi.macAddress()
     *
     * On a chip with no radio of its own (ESP32-P4, WiFi over ESP-Hosted) the
     * station address is not in the chip's MAC table and the read fails: the
     * address belongs to the co-processor. _macDefaultKnown then stays false
     * and the adapter reports the address once the link is up
     * (setFactoryMac()).
     *
     * Generates _hostnameDefault with prefix "ESP".
     */
    void _initMacDefault() override {
#if defined(ARDUINO_ARCH_ESP32)
#  if SOC_WIFI_SUPPORTED
        _macDefaultKnown = (esp_read_mac(_macDefault, ESP_MAC_WIFI_STA) == ESP_OK)
                        && isValidMac(_macDefault);
        if (!_macDefaultKnown) memset(_macDefault, 0, MAC_LEN);
#  else
        // No radio on this chip, so no station address in its MAC table:
        // asking would only fail, and log "mac type is incorrect" on the way.
        // Known once the adapter has read it from the co-processor.
        memset(_macDefault, 0, MAC_LEN);
        _macDefaultKnown = false;
#  endif
        _generateHostnameDefault("ESP");
#elif defined(ARDUINO_ARCH_ESP8266)
        uint8_t mac[MAC_LEN];
        WiFi.macAddress(mac);
        memcpy(_macDefault, mac, MAC_LEN);
        _macDefaultKnown = isValidMac(_macDefault);
        _generateHostnameDefault("ESP");
#else
        memset(_macDefault, 0, MAC_LEN);
        strncpy(_hostnameDefault, "UNKNOWN", MAX_HOSTNAME_SIZE);
#endif
    }

    // ------------------------------------------------------------------------
    // Persistence hooks
    // ------------------------------------------------------------------------

    /**
     * @brief Saves WiFi credentials to an already-open Preferences object.
     *
     * Calls NetworkProfile::_doSave(p) first to persist base fields, then
     * writes SSID, password and transmit power under keys "ssid", "pass"
     * and "txpwr".
     *
     * @param p Open, writable Preferences object.
     * @return true if all fields were written successfully,
     *         false on any error.
     */
    bool _doSave(Preferences& p) override {
        bool ok = NetworkProfile::_doSave(p);
        ok = (_putStr(p, "ssid", _ssid)     > 0) && ok;
        ok = (_putStr(p, "pass", _password) > 0) && ok;
        ok = (p.putFloat("txpwr", _txPower) > 0) && ok;
        return ok;
    }

    /**
     * @brief Loads WiFi credentials from an already-open Preferences object.
     *
     * Calls NetworkProfile::_doLoad(p) first to restore base fields, then
     * reads SSID, password and transmit power from keys "ssid", "pass"
     * and "txpwr".
     *
     * The transmit power falls back to DEFAULT_WIFI_TX_POWER_dBm when the key
     * is missing or holds a value this build will not accept. Both happen in
     * practice: a record written before "txpwr" existed has no key (and
     * Preferences::getFloat() answers a missing key with NAN unless given a
     * default, which would poison _txPower and make every later
     * setTxPower() comparison fail), and the accepted range is not the same on
     * both platforms — ESP32 takes discrete levels up to 19.5 dBm, ESP8266 a
     * continuous range up to 20.5 — so a namespace carried between them, or a
     * build whose WIFI_PROFILE_DEFAULT_WIFI_TX_POWER changed, can hold a value
     * that is out of range here. Falling back beats storing something the radio
     * will refuse.
     *
     * SSID and password are emptied before they are read, for the reason given
     * in NetworkProfile::_doLoad(): an absent key leaves the destination
     * untouched, and _putStr() removes the key of an empty value — so clearing
     * the credentials, saving, and loading back into the same object would
     * otherwise restore them.
     *
     * @param p Open, read-only Preferences object.
     * @return true if all fields were read,
     *         false on fail of NetworkProfile::_doLoad(p).
     */
    bool _doLoad(Preferences& p) override {
        if (!NetworkProfile::_doLoad(p)) return false;
        _ssid[0]     = '\0';
        _password[0] = '\0';
        p.getString("ssid", _ssid,     MAX_SSID_SIZE);
        p.getString("pass", _password, MAX_PASSWORD_SIZE);
        const float pwr = p.getFloat("txpwr", DEFAULT_WIFI_TX_POWER_dBm);
        _txPower = isValidTxPower(pwr) ? pwr : DEFAULT_WIFI_TX_POWER_dBm;
        return true;
    }

    /**
     * @brief Appends WiFi-specific fields to the JSON buffer.
     *
     * Appends "ssid" to the in-progress JSON object. The SSID is escaped
     * via _jsonEscapeStr() to produce valid JSON even if the SSID contains
     * non-printable or special characters (RFC 8259).
     *
     * The password is intentionally omitted for security reasons.
     *
     * @param json JSON buffer being built.
     * @param len  Total capacity of @p json in bytes.
     * @return true if content fit, false on truncation.
     */
    bool _doJson(char* json, size_t len) const override {
        // SSID may contain non-printable characters per IEEE 802.11 (binary
        // field). Escape to \uXXXX sequences for valid JSON output (RFC 8259).
        // Worst case: all 32 bytes escaped → 32 × 6 = 192 chars + null
        // terminator.
        constexpr size_t ESC_SIZE = MAX_SSID_LEN * 6 + 1;
        char escaped[ESC_SIZE];
        _jsonEscapeStr(escaped, sizeof(escaped), _ssid);
        // +12 for: ,"ssid":"" and null terminator
        char buf[ESC_SIZE + 12];
        bool ok = _fitted(snprintf_P(buf, sizeof(buf), PSTR(",\"ssid\":\"%s\""),
                                     escaped), sizeof(buf));
        ok = ok && NetworkProfile::_strncatJson(json, buf, len);
        // Password is intentionally omitted for security
        // Two decimals, not %f's six: the accepted values sit on a 0.25 dBm
        // grid (isValidTxPower() enforces that on both platforms), so two are
        // exactly enough and the remaining four were always zeros. One would
        // not do — 13.25 would come back as 13.2, and on ESP8266 the whole
        // grid is offered, not just the halves ESP32's level set happens to
        // use.
        ok = ok && _fitted(snprintf_P(buf, sizeof(buf), PSTR(",\"txpwr\":%.2f"),
                                      _txPower), sizeof(buf));
        ok = ok && NetworkProfile::_strncatJson(json, buf, len);
        return ok;
    }

private:
    char  _ssid    [MAX_SSID_SIZE];
    char  _password[MAX_PASSWORD_SIZE];
    float _txPower;
};

#endif // !ARDUINO_ARCH_AVR
