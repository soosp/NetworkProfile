#pragma once

/**
 * @file NetworkProfileVersion.h
 * @brief Compile-time version of the NetworkProfile library.
 *
 * Encoded as major * 10000 + minor * 100 + patch, the same scheme
 * AsyncConfigPortal's FIRMWARE_VERSION uses. 0.8.0 is 800.
 *
 * It exists so that a consumer can require a minimum version at compile time.
 * That matters because the libraries built on this one deliberately do not
 * declare it as a dependency — AsyncConfigPortal's network and MQTT modules are
 * opt-in, and a manifest entry would force the profile family on every project
 * that only wants the portal. With no way to express the constraint in
 * library.json, the check has to live in the source:
 *
 * @code
 * #if !defined(NETWORK_PROFILE_VERSION) || NETWORK_PROFILE_VERSION < 800
 * #  error "This module needs NetworkProfile 0.8.0 or newer"
 * #endif
 * @endcode
 *
 * An undefined macro evaluates to 0 in #if, so the same test also catches the
 * versions released before this header existed.
 */
#define NETWORK_PROFILE_VERSION 900
