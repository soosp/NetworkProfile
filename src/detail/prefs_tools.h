#pragma once

/**
 * @file prefs_tools.h
 * @brief Preferences helpers shared by the profile classes in this library.
 *
 * Internal implementation detail — do not include directly.
 */

#include <Arduino.h>

// The single place this library decides what Preferences is; every profile class
// reaches it through here.
//
// AVR is named rather than left to #else. EEPROMPreferences.h declares its class
// unconditionally and only its .cpp is AVR-guarded, so on a fourth architecture
// an #else branch would compile and then fail at link time with an undefined
// reference to EEPROMPreferences::begin — a long way from the actual cause.
// Saying which architectures are supported fails at the include instead.
//
// No `using Preferences = EEPROMPreferences;` here, and none in NetworkProfile.h
// either: EEPROMPreferences.h has declared it itself since 0.2.0. Repeating it
// was legal but overrode that header's EEPROMPREFERENCES_NO_PREFERENCES_ALIAS
// opt-out, which is a working extension point — an application can define the
// macro, alias Preferences to its own EEPROMPreferences-compatible type (a
// different EEPROM offset, say), include this library after it, and the profile
// classes will store through the application's type.
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
#  include <Preferences.h>
#elif defined(ARDUINO_ARCH_AVR)
#  include <EEPROMPreferences.h>
#else
#  error "NetworkProfile requires ESP32, ESP8266 or AVR. Porting it to another " \
         "architecture means giving it a Preferences implementation: either a " \
         "native one on the include path, or a class with the same interface " \
         "aliased to Preferences, added as a branch here."
#endif

/**
 * @name Quiet access to keys that may be absent
 *
 * An absent key is a normal state here, not an error: a save removes the keys
 * a profile does not use (the static address under DHCP, an empty string, a MAC
 * without an override), and a load then reads them back as absent. The ESP32
 * core's Preferences logs every such remove(), getString() and getBytes() at
 * error level ("nvs_erase_key fail: ip NOT_FOUND", "nvs_get_str len fail: host
 * NOT_FOUND"): half a dozen red lines per save, which bury a real failure and
 * teach the reader to ignore them. These ask isKey() first, which is silent.
 * The numeric getters log at verbose level only and need no wrapper.
 *
 * The absence itself is logged at debug level instead (log_d: compiled in only
 * with CORE_DEBUG_LEVEL 4 or above, so a release build carries neither the
 * strings nor the calls). The check is made on ESP32 only, the core whose
 * Preferences logs these; elsewhere (ESP8266, AVR) the calls go straight
 * through, as before.
 * @{
 */
inline void prefs_remove(Preferences& p, const char* key) {
#if defined(ARDUINO_ARCH_ESP32)
    if (!p.isKey(key)) { log_d("%s: not stored, nothing to remove", key); return; }
#endif
    p.remove(key);
}

/// getString() into @p buf; @p buf is left alone when the key is absent.
inline size_t prefs_getStr(Preferences& p, const char* key, char* buf, size_t len) {
#if defined(ARDUINO_ARCH_ESP32)
    if (!p.isKey(key)) { log_d("%s: not stored, default kept", key); return 0; }
#endif
    return p.getString(key, buf, len);
}

/// getBytes() into @p buf; @p buf is left alone when the key is absent.
inline size_t prefs_getBytes(Preferences& p, const char* key, void* buf, size_t len) {
#if defined(ARDUINO_ARCH_ESP32)
    if (!p.isKey(key)) { log_d("%s: not stored, default kept", key); return 0; }
#endif
    return p.getBytes(key, buf, len);
}
/** @} */

/**
 * @brief Stores a string, treating "empty" as "no value" rather than as failure.
 *
 * putString() returns the length written, and an empty string is zero of them —
 * so the usual `> 0` success test reports a correctly stored empty value as an
 * error. Non-empty values are written and must succeed; an empty one removes the
 * key instead, so a later load falls back to the field's default rather than to
 * a stored blank.
 *
 * @param p     Open, writable Preferences object.
 * @param key   Target key name.
 * @param value String to store; may be empty.
 * @return true if the value was written, or the key removed for an empty one.
 */
inline bool prefs_putStr(Preferences& p, const char* key, const char* value) {
    if (value && value[0] != '\0') return p.putString(key, value) > 0;
    prefs_remove(p, key);
    return true;
}
