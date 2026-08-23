#pragma once

/**
 * @file ntp_config.h
 * @brief The one macro that decides whether NTP exists in a build.
 *
 * Internal implementation detail — include NtpProfile.h, not this.
 *
 * Separate from NtpProfile.h because two different questions are asked of it.
 * NtpProfile.h asks "how many servers do I hold", and refuses to be included at
 * all when the answer is none. NetworkManager asks "should I compile my NTP
 * support", and has to be able to ask that when the answer is no — so it needs
 * the macro without the class, which is what this header provides.
 */

// NETWORK_PROFILE_NTP_SERVER_COUNT names a profile that does not hold the
// servers, so it has no effect here. Reported rather than ignored: a build still
// passing it would silently get the default count instead of the intended one.
#ifdef NETWORK_PROFILE_NTP_SERVER_COUNT
#  warning "NETWORK_PROFILE_NTP_SERVER_COUNT is obsolete - use NTP_PROFILE_SERVER_COUNT. The old name has no effect."
#endif

/**
 * @brief Whether NTP exists in this build at all.
 *
 * Separate from the server count because the two are different questions. The
 * count sizes a profile; this decides whether there is any NTP code to size a
 * profile for — including NetworkManager's own SNTP support, which on AVR is a
 * UDP client with its own socket and state machine rather than a buffer.
 *
 * AVR is opt-in for that reason; elsewhere the cost is a little flash.
 */
// Zero used to mean "no NTP". It now has no valid meaning: the count sizes a
// profile, and a profile holding no servers is a contradiction. Caught here so
// an existing build says what to change instead of failing further in.
#if defined(NTP_PROFILE_SERVER_COUNT) && (NTP_PROFILE_SERVER_COUNT == 0)
#  error "NTP_PROFILE_SERVER_COUNT=0 no longer disables NTP. Set NTP_PROFILE_ENABLED to 0 instead, and leave the count at 1..3."
#endif

#ifndef NTP_PROFILE_ENABLED
#  if defined(ARDUINO_ARCH_AVR)
#    define NTP_PROFILE_ENABLED 0
#  else
#    define NTP_PROFILE_ENABLED 1
#  endif
#endif

#if NTP_PROFILE_ENABLED

/**
 * @brief How many servers a profile stores.
 *
 * Three is what SNTP accepts, and a second and third are what let a sync survive
 * one server being unreachable — but each costs Host::MAX_FQDN_SIZE of RAM, so
 * only ESP32 takes them by default. Everywhere else one is the default, and a
 * project with room for more asks for it.
 *
 * A count of zero is not a way to switch NTP off: set NTP_PROFILE_ENABLED to 0.
 */
#  ifndef NTP_PROFILE_SERVER_COUNT
#    if defined(ARDUINO_ARCH_ESP32)
#      define NTP_PROFILE_SERVER_COUNT 3
#    else
#      define NTP_PROFILE_SERVER_COUNT 1
#    endif
#  endif

#  if (NTP_PROFILE_SERVER_COUNT < 1) || (NTP_PROFILE_SERVER_COUNT > 3)
#    error "NTP_PROFILE_SERVER_COUNT must be 1..3 (SNTP accepts at most three). To leave NTP out of the build entirely, set NTP_PROFILE_ENABLED to 0."
#  endif

#endif  // NTP_PROFILE_ENABLED
