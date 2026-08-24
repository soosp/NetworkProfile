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
    p.remove(key);
    return true;
}
