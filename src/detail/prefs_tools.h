#pragma once

/**
 * @file prefs_tools.h
 * @brief Preferences helpers shared by the profile classes in this library.
 *
 * Internal implementation detail — do not include directly.
 */

#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
#  include <Preferences.h>
#else
#  include <EEPROMPreferences.h>
   using Preferences = EEPROMPreferences;
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
