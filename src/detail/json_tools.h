#pragma once

/**
 * @file json_tools.h
 * @brief Internal helpers shared by the JSON builders in NetworkProfile and
 *        NetworkManager.
 *
 * This file is an internal implementation detail — do not include directly
 * from application code.
 *
 * Two things live here. First, a truncation check for the snprintf() family.
 * Second, bounded append functions, in two flavours that differ in what they do
 * when the text does not fit:
 *
 *   - json_cat()       appends what fits, then reports failure (truncating);
 *   - json_catAtomic() writes nothing at all, and reports failure.
 *
 * Both behaviours were already in use — NetworkProfile::toJson() built its
 * document with the truncating one, NetworkManager::statusToJson() with the
 * atomic one — so both are kept rather than silently unified: a caller that
 * expects an unchanged buffer on failure must not get a half-written one.
 *
 * Every function comes in a plain and a `_P` variant. The split is deliberate:
 * a `const char*` does not say which address space it points into, so the call
 * site, not the callee, states where the source lives. Handing program space to
 * a RAM function reads flash byte by byte, which faults on ESP8266 and AVR.
 */

#include <Arduino.h>
#include <string.h>

/**
 * @brief Did an snprintf()-family call write its whole string?
 *
 * snprintf() returns an int: negative on an encoding error, otherwise the length
 * the output *would* have needed — so a value at or above the buffer size means
 * it was truncated. Comparing that int against a size_t directly is a signed/
 * unsigned comparison, and a negative result would wrap into a huge unsigned
 * value; this keeps the two failure modes explicit.
 *
 * @param written Return value of the snprintf() call.
 * @param bufSize Size of the destination buffer.
 * @return @c true if the whole string fit.
 */
inline bool json_fitted(int written, size_t bufSize) {
    return written >= 0 && static_cast<size_t>(written) < bufSize;
}

/**
 * @brief Appends @p src to @p dst, truncating at @p dstSize if it does not fit.
 * @param dst     Destination string in RAM (null-terminated).
 * @param src     String to append, in RAM.
 * @param dstSize Total capacity of @p dst in bytes.
 * @return @c true if all of @p src fit, @c false if the output was truncated.
 */
inline bool json_cat(char* dst, const char* src, size_t dstSize) {
    const size_t cur = strlen(dst);
    const size_t remaining = (cur < dstSize) ? (dstSize - cur - 1) : 0;
    if (remaining == 0) return false;
    const size_t toAppend = strlen(src);
    strncat(dst, src, remaining);
    return toAppend <= remaining;
}

/**
 * @brief Program-space counterpart of json_cat().
 * @param dst     Destination string in RAM (null-terminated).
 * @param src     String to append, in program space (PSTR/PROGMEM).
 * @param dstSize Total capacity of @p dst in bytes.
 * @return @c true if all of @p src fit, @c false if the output was truncated.
 */
inline bool json_cat_P(char* dst, PGM_P src, size_t dstSize) {
    const size_t cur = strlen(dst);
    const size_t remaining = (cur < dstSize) ? (dstSize - cur - 1) : 0;
    if (remaining == 0) return false;
    const size_t toAppend = strlen_P(src);
    strncat_P(dst, src, remaining);
    return toAppend <= remaining;
}

/**
 * @brief Appends @p src to @p dst only if all of it fits; otherwise leaves
 *        @p dst untouched.
 * @param dst     Destination string in RAM (null-terminated).
 * @param src     String to append, in RAM.
 * @param dstSize Total capacity of @p dst in bytes.
 * @return @c true if appended, @c false if it would not fit (nothing written).
 */
inline bool json_catAtomic(char* dst, const char* src, size_t dstSize) {
    const size_t cur = strlen(dst);
    if (cur >= dstSize) return false;
    const size_t n = strlen(src);
    if (n + 1 > dstSize - cur) return false;   // no room for src + NUL
    memcpy(dst + cur, src, n + 1);
    return true;
}

/**
 * @brief Program-space counterpart of json_catAtomic().
 * @param dst     Destination string in RAM (null-terminated).
 * @param src     String to append, in program space (PSTR/PROGMEM).
 * @param dstSize Total capacity of @p dst in bytes.
 * @return @c true if appended, @c false if it would not fit (nothing written).
 */
inline bool json_catAtomic_P(char* dst, PGM_P src, size_t dstSize) {
    const size_t cur = strlen(dst);
    if (cur >= dstSize) return false;
    const size_t n = strlen_P(src);
    if (n + 1 > dstSize - cur) return false;   // no room for src + NUL
    memcpy_P(dst + cur, src, n + 1);
    return true;
}

/**
 * @brief Escapes @p src into @p dst so it can go inside a JSON string.
 *
 * Printable ASCII is copied through; the quote, the backslash and everything
 * outside 0x20..0x7E become \uXXXX escapes (RFC 8259). Truncates rather than
 * overrunning, and never leaves a half-written escape.
 *
 * Shared because more than one builder needs it: a Wi-Fi SSID is arbitrary
 * bytes, so both the stored profile and the live link status must escape it, and
 * two implementations of that would be two chances to emit invalid JSON.
 *
 * @param dst    Destination buffer.
 * @param dstLen Size of @p dst in bytes.
 * @param src    Null-terminated source string.
 * @return Bytes written to @p dst, excluding the null terminator.
 */
inline size_t json_escape(char* dst, size_t dstLen, const char* src) {
    if (!dst || !src || dstLen == 0) return 0;
    size_t written = 0;
    while (*src && written + 1 < dstLen) {
        unsigned char c = (unsigned char)*src++;
        if (c >= 0x20 && c <= 0x7E && c != '"' && c != '\\') {
            dst[written++] = c;                       // printable ASCII
        } else {
            if (written + 6 >= dstLen) break;         // no room for \uXXXX
            written += snprintf(dst + written, dstLen - written, "\\u%04X", c);
        }
    }
    dst[written] = '\0';
    return written;
}

/**
 * @brief Appends @p src to @p dst, escaping as it goes.
 *
 * The same result as escaping into a scratch buffer and appending that, without
 * the scratch buffer — which matters where this runs: a fully escaped host name
 * needs six bytes per character, and a temporary that size does not belong on an
 * ESP8266 stack, let alone inside an async request handler.
 *
 * @param dst     Destination string in RAM (null-terminated).
 * @param src     String to append, in RAM; escaped per RFC 8259.
 * @param dstSize Total capacity of @p dst in bytes.
 * @return @c true if all of @p src fit; @c false if it was truncated, in which
 *         case @p dst is left with whatever fit, terminated and never split
 *         mid-escape.
 */
inline bool json_cat_esc(char* dst, const char* src, size_t dstSize) {
    if (!dst || !src) return false;
    size_t w = strlen(dst);
    while (*src) {
        const unsigned char c = (unsigned char)*src++;
        if (c >= 0x20 && c <= 0x7E && c != '"' && c != '\\') {
            if (w + 1 >= dstSize) { dst[w] = '\0'; return false; }
            dst[w++] = (char)c;
        } else {
            if (w + 6 >= dstSize) { dst[w] = '\0'; return false; }
            w += snprintf(dst + w, dstSize - w, "\\u%04X", c);
        }
    }
    dst[w] = '\0';
    return true;
}
