/*
 * Header-only: the Arduino IDE compiles libraries separately from the sketch,
 * so a precompiled unit would not see the configuration macros you define —
 * being header-only, the library is compiled with each includer's macros
 * instead. In a multi-file project, define those macros globally so every
 * translation unit agrees (see the README).
 */

#pragma once
#include "NetworkProfile.h"
#ifdef ARDUINO_ARCH_ESP32
#  include <esp_mac.h>
#elif defined(ARDUINO_ARCH_ESP8266)
#  include <ESP8266WiFi.h>   // WiFi.macAddress() — the wired MAC is derived from it
#elif defined(ARDUINO_ARCH_AVR)
#  include "detail/avr_mac.h"
#endif

/**
 * @brief Network profile for Ethernet interfaces.
 *
 * Extends NetworkProfile with Ethernet-specific behaviour. Currently no
 * additional fields are required (Ethernet has no authentication).
 * The class is intentionally left open for future extensions such as
 * VLAN tagging or MAC address override.
 *
 * Applying the profile to the hardware interface (ETH.begin(), ETH.config())
 * is the responsibility of NetworkManager, not this class.
 *
 * Persistence:
 *   - On ESP32/ESP8266: IP configuration is saved/loaded via the base class
 *     _doSave() / _doLoad() mechanism.
 *   - On AVR: the base class save/load callback mechanism is used.
 */
class EthProfile : public NetworkProfile {
public:
    /**
     * @brief Constructs an EthProfile with default values.
     *
     * Initialises the base class. DHCP is enabled by default.
     */
    EthProfile()
        : NetworkProfile()
    {
        _initMacDefault();
    }

    /**
     * @brief Constructs an EthProfile from a NetworkConfig struct.
     * @param cfg Initial base configuration to apply.
     */
    explicit EthProfile(const NetworkConfig& cfg)
        : NetworkProfile(cfg)
    {
        _initMacDefault();
    }

    /** @brief Destructor. */
    ~EthProfile() override = default;

    /**
     * @brief Returns InterfaceType::ETH.
     *
     * Compile-time constant — no mutex needed.
     */
    InterfaceType getInterfaceType() const override {
        return InterfaceType::ETH;
    }

protected:
    // ------------------------------------------------------------------------
    // MAC initialisation
    // ------------------------------------------------------------------------

    /**
     * @brief Initialises the default MAC address for the Ethernet interface.
     *
     * Platform-specific implementation:
     * - ESP32:   esp_read_mac(_macDefault, ESP_MAC_ETH)
     * - ESP8266: not supported, _macDefault remains zero
     * - ATmega328PB: boot_signature_byte_get() based generation
     * - Other AVR: NETWORK_PROFILE_DEFAULT_ETH_MAC string via macFromStr()
     *
     * Generates _hostnameDefault with prefix "ETH" or "AVR".
     */
    void _initMacDefault() override {
#if defined(ARDUINO_ARCH_ESP32)
        esp_read_mac(_macDefault, ESP_MAC_ETH);
        _generateHostnameDefault("ESP");

#elif defined(ARDUINO_ARCH_ESP8266)
        // ESP8266 has no dedicated Ethernet MAC. Derive a stable, locally-
        // administered address from the WiFi STA MAC: setting the U/L bit
        // and clearing the I/G bit in byte 0 yields a unicast LAA that differs
        // from the WiFi MAC, so the wired interface never collides with WiFi
        // when both are up. A user-supplied NETWORK_PROFILE_DEFAULT_ETH_MAC
        // overrides it.
#ifdef NETWORK_PROFILE_DEFAULT_ETH_MAC
        NetworkProfile::macFromStr(NETWORK_PROFILE_DEFAULT_ETH_MAC, _macDefault);
#else
        WiFi.macAddress(_macDefault);
        _macDefault[0] = (_macDefault[0] | 0x02) & 0xFE;  // bit1=1 (LAA), bit0=0 (unicast) — byte 0
#endif
        _generateHostnameDefault("ESP");

#elif defined(ARDUINO_ARCH_AVR)
        avr_init_mac_default(_macDefault);
        _generateHostnameDefault("AVR");

#else
        memset(_macDefault, 0, MAC_LEN);
        strncpy(_hostnameDefault, "UNKNOWN", MAX_HOSTNAME_SIZE);
#endif
    }

    // ------------------------------------------------------------------------
    // Persistence hooks
    // ------------------------------------------------------------------------

#if !defined(ARDUINO_ARCH_AVR)
    /**
     * @brief Saves Ethernet-specific fields to an already-open Preferences
     *        object.
     *
     * Currently delegates entirely to NetworkProfile::_doSave(p) as there
     * are no Ethernet-specific fields beyond the base IP configuration.
     * Override in a subclass to add fields such as VLAN or MAC override.
     *
     * @param p Open, writable Preferences object.
     * @return The result of NetworkProfile::_doSave().
     */
    bool _doSave(Preferences& p) override {
        // No Ethernet-specific fields yet — delegate to base class.
        // Add Ethernet-specific keys here when extending (e.g. VLAN, MAC
        // override).
        return NetworkProfile::_doSave(p);
    }

    /**
     * @brief Loads Ethernet-specific fields from an already-open Preferences
     *        object.
     *
     * Currently delegates entirely to NetworkProfile::_doLoad(p).
     * Override in a subclass to restore fields such as VLAN or MAC override.
     *
     * @param p Open, read-only Preferences object.
     * @return The result of NetworkProfile::_doLoad().
     */
    bool _doLoad(Preferences& p) override {
        // No Ethernet-specific fields yet — delegate to base class.
        // Add Ethernet-specific keys here when extending (e.g. VLAN, MAC
        // override).
        return NetworkProfile::_doLoad(p);
    }
#endif

};