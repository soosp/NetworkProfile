#pragma once

/**
 * @file avr_mac.h
 * @brief Internal helper for AVR platform MAC address generation.
 *
 * This file is an internal implementation detail — do not include directly
 * from application code.
 *
 * Supported platforms:
 *   - ATmega328PB, ATmega48PB, ATmega88PB, ATmega168PB: guaranteed unique
 *     serial number via signature bytes (Microchip AN000011836).
 *   - Other AVR: uses NETWORK_PROFILE_DEFAULT_ETH_MAC string (user-defined).
 *
 * @note The generated MAC address is a Locally Administered Address (LAA).
 *       Bit 1 of byte 0 is forced to 1 (locally administered) and bit 0 is
 *       forced to 0 (unicast), per IEEE 802 MAC address conventions:
 *         - Bit 0 of byte 0: 0 = unicast
 *                            1 = multicast
 *         - Bit 1 of byte 0: 0 = globally unique (OUI)
 *                            1 = locally administered
 *       An LAA is not guaranteed globally unique but is safe for local network
 *       use. On PB-series AVR chips the underlying serial number bytes are
 *       factory-unique, making the resulting LAA practically unique.
 */

#if defined(ARDUINO_ARCH_AVR)
#  include <avr/boot.h>
#endif

#include "NetworkProfile.h"

/**
 * @brief Fills @p mac with a platform-specific default MAC address on AVR.
 *
 * On ATmega328PB/48PB/88PB/168PB: reads signature bytes and constructs a
 * Locally Administered Address (LAA). Byte 0 is forced to unicast LAA
 * by setting bit 1 and clearing bit 0.
 *
 * On other AVR: parses NETWORK_PROFILE_DEFAULT_ETH_MAC string via
 * NetworkProfile::macFromStr(). Triggers a compile-time error if
 * NETWORK_PROFILE_DEFAULT_ETH_MAC is not defined.
 *
 * @param mac Destination buffer (NetworkProfile::MAC_LEN bytes).
 */
inline void avr_init_mac_default(uint8_t* mac) {
#if defined(__AVR_ATmega328PB__) || \
    defined(__AVR_ATmega48PB__)  || \
    defined(__AVR_ATmega88PB__)  || \
    defined(__AVR_ATmega168PB__)
    // Factory-unique serial number — Locally Administered Address (LAA)
    // Byte addresses follow Microchip AN000011899 (Lot/Wafer/XY coordinates).
    // Byte 0: bit1=1 (locally administered), bit0=0 (unicast), per IEEE 802.
    mac[0] = (boot_signature_byte_get(0x10) | 0x02) & 0xFE;
    mac[1] = boot_signature_byte_get(0x13);
    mac[2] = boot_signature_byte_get(0x12);
    mac[3] = boot_signature_byte_get(0x15);
    mac[4] = boot_signature_byte_get(0x16);
    mac[5] = boot_signature_byte_get(0x17);

#elif defined(ARDUINO_ARCH_AVR)
    #ifndef NETWORK_PROFILE_DEFAULT_ETH_MAC
        #error "NETWORK_PROFILE_DEFAULT_ETH_MAC must be defined for this AVR platform. " \
               "Example: #define NETWORK_PROFILE_DEFAULT_ETH_MAC \"4E:52:47:30:30:31\""
    #endif
    NetworkProfile::macFromStr(NETWORK_PROFILE_DEFAULT_ETH_MAC, mac);
#endif
}
