#pragma once

/**
 * @file SyslogProfile.h
 * @brief Stored syslog target settings: server, port, severity floor, facility.
 *
 * Sits in this library for the same reason the MQTT broker and the NTP servers
 * do: it is something a networked device stores in order to reach a service,
 * and splitting it into its own library would separate things that are always
 * deployed together. It knows nothing about syslog itself — no socket, no
 * framing, no message. A sender (for example SyslogSender) reads this; this
 * owns what survives a reboot.
 *
 * Extending it follows the MqttProfile pattern — override the virtual hooks.
 */

#include <Arduino.h>
#include <type_traits>

#if !defined(ARDUINO_ARCH_AVR) && !defined(ARDUINO_ARCH_ESP8266)
#  include <mutex>
#  include <chrono>
#endif

#include "NetworkProfileVersion.h"
#include "Host.h"
#include "detail/json_tools.h"
#include "detail/prefs_tools.h"

/** @brief How long a bulk operation waits for the lock, in ms. */
#ifndef SYSLOG_PROFILE_MUTEX_TIMEOUT
#  define SYSLOG_PROFILE_MUTEX_TIMEOUT 1000
#endif

class SyslogProfile {
public:
    static constexpr uint32_t MUTEX_TIMEOUT = SYSLOG_PROFILE_MUTEX_TIMEOUT;

    static constexpr uint16_t DEFAULT_PORT = 514;   ///< syslog over UDP

    /** @brief RFC 5424 severity codes, the values a sender uses on the wire. */
    enum Severity : uint8_t {
        SEV_EMERGENCY = 0, SEV_ALERT = 1, SEV_CRITICAL = 2, SEV_ERROR = 3,
        SEV_WARNING = 4, SEV_NOTICE = 5, SEV_INFO = 6, SEV_DEBUG = 7,
    };

    /** @brief RFC 5424 facility codes; the local ones are what a device uses. */
    static constexpr uint8_t FAC_USER   = 1;
    static constexpr uint8_t FAC_LOCAL0 = 16;
    static constexpr uint8_t FAC_LOCAL7 = 23;
    static constexpr uint8_t FAC_MAX    = 23;

    /** @brief The stored settings. */
    struct SyslogConfig {
        /**
         * Whether the application should send at all. Separate from an empty
         * host on purpose, as in MqttProfile: "switched off" and "not
         * configured yet" are two different facts.
         */
        bool     enabled = false;

        char     host[Host::MAX_FQDN_SIZE] = {};  ///< Server: FQDN or IP literal.
        uint16_t port    = DEFAULT_PORT;

        /**
         * Lowest severity worth sending, as an RFC 5424 code: a message whose
         * severity is numerically greater (less severe) is not sent. INFO by
         * default, which forwards errors, warnings and milestones and keeps
         * debug output on the console where it belongs.
         */
        uint8_t  minSeverity = SEV_INFO;

        /// Facility for every message. LOCAL0 by default.
        uint8_t  facility = FAC_LOCAL0;
    };

    /** @brief Result of checkConfig(). */
    enum class ConfigCheck : uint8_t {
        Ok = 0,        ///< Usable as it stands.
        NoHost,        ///< Enabled but no server address.
        BadHost,       ///< Not a valid FQDN or IP literal.
        BadPort,       ///< Port is zero.
        BadSeverity,   ///< Not an RFC 5424 severity (0..7).
        BadFacility,   ///< Not an RFC 5424 facility (0..23).
    };

    /** @brief Names a ConfigCheck value, for logs and configuration UIs.
     *  Flash-resident, so printing costs no RAM on AVR. */
    static const __FlashStringHelper* configCheckName(ConfigCheck c) {
        switch (c) {
            case ConfigCheck::Ok:          return F("ok");
            case ConfigCheck::NoHost:      return F("no server address");
            case ConfigCheck::BadHost:     return F("invalid server address");
            case ConfigCheck::BadPort:     return F("invalid port");
            case ConfigCheck::BadSeverity: return F("invalid severity");
            case ConfigCheck::BadFacility: return F("invalid facility");
        }
        return F("unknown");
    }

    /** @brief Names a severity code, for UIs. Unknown codes name as "?". */
    static const __FlashStringHelper* severityName(uint8_t s) {
        switch (s) {
            case SEV_EMERGENCY: return F("emergency");
            case SEV_ALERT:     return F("alert");
            case SEV_CRITICAL:  return F("critical");
            case SEV_ERROR:     return F("error");
            case SEV_WARNING:   return F("warning");
            case SEV_NOTICE:    return F("notice");
            case SEV_INFO:      return F("info");
            case SEV_DEBUG:     return F("debug");
        }
        return F("?");
    }

    virtual ~SyslogProfile() = default;

    /**
     * @brief Copies the current settings out. Locked, so a save landing in the
     *        middle cannot hand out half of each field. @p out is left
     *        untouched if the lock is not obtained.
     */
    bool getConfig(SyslogConfig& out) const {
        if (!_lock()) return false;
        out = _cfg;
        _unlock();
        return true;
    }

    /**
     * @brief Replaces the current settings. Validated as a whole through
     *        checkConfig(); nothing is written unless the whole struct passes.
     * @return false if @p in was refused or the lock could not be obtained.
     */
    bool setConfig(const SyslogConfig& in) {
        if (checkConfig(in) != ConfigCheck::Ok) return false;
        if (!_lock()) return false;
        _cfg = in;
        _unlock();
        return true;
    }

    /** @brief Refuses a subclass's config, so the omission cannot be silent
     *  (see MqttProfile for the reasoning). */
    template <class T, class = typename std::enable_if<
                  std::is_base_of<SyslogConfig, T>::value &&
                  !std::is_same<SyslogConfig, T>::value>::type>
    ConfigCheck checkConfig(const T&) const = delete;

    /** @copydoc checkConfig(const T&) */
    template <class T, class = typename std::enable_if<
                  std::is_base_of<SyslogConfig, T>::value &&
                  !std::is_same<SyslogConfig, T>::value>::type>
    bool setConfig(const T&) = delete;

    /**
     * @brief Checks the settings for internal consistency. A disabled profile
     *        always passes: its fields are not in use, and refusing to save a
     *        half-filled form the user has just switched off would be
     *        unhelpful. Virtual, so a subclass's rules run through a base
     *        reference.
     */
    virtual ConfigCheck checkConfig(const SyslogConfig& cfg) const {
        if (!cfg.enabled) return ConfigCheck::Ok;
        if (cfg.host[0] == '\0') return ConfigCheck::NoHost;
        if (!Host::isValidIp(cfg.host) && !Host::isValidFqdn(cfg.host))
            return ConfigCheck::BadHost;
        if (cfg.port == 0) return ConfigCheck::BadPort;
        if (cfg.minSeverity > SEV_DEBUG) return ConfigCheck::BadSeverity;
        if (cfg.facility > FAC_MAX) return ConfigCheck::BadFacility;
        return ConfigCheck::Ok;
    }

    /** @brief Writes the settings to @p ns. */
    bool saveCfg(const char* ns) {
        if (!ns) return false;
        if (!_lock()) return false;
        Preferences p;
        if (!p.begin(ns, false)) { _unlock(); return false; }
        bool ok = p.putBool("_ok", false);      // invalid until fully written
        if (ok) ok = _doSave(p);
        if (ok) ok = p.putBool("_ok", true);
        p.end();
        _unlock();
        return ok;
    }

    /** @brief Reads the settings from @p ns.
     *  @return true if a complete record was found; false leaves the defaults. */
    bool loadCfg(const char* ns) {
        if (!ns) return false;
        if (!_lock()) return false;
        Preferences p;
        if (!p.begin(ns, true)) { _unlock(); return false; }
        bool ok = p.getBool("_ok", false);       // ignore a half-written record
        if (ok) ok = _doLoad(p);
        p.end();
        _unlock();
        return ok;
    }

    /** @brief Erases the whole namespace @p ns — what a factory reset needs.
     *  Clears the store, not this object. */
    bool clearCfg(const char* ns) {
        if (!ns) return false;
        if (!_lock()) return false;
        Preferences p;
        bool ok = p.begin(ns, false);
        if (ok) { ok = p.clear(); p.end(); }
        _unlock();
        return ok;
    }

    /**
     * @brief Renders the settings as a JSON object, for backup. Nothing here
     *        is a secret, so every field is included.
     * @return true if the whole document fit.
     */
    virtual bool toJson(char* json, size_t len) const {
        if (!json || len == 0) return false;

        char buf[48];
        json[0] = '\0';
        if (!_lock()) return false;

        bool ok = json_cat_P(json, PSTR("{\"enabled\":"), len);
        ok = ok && json_cat_P(json, _cfg.enabled ? PSTR("true") : PSTR("false"), len);

        ok = ok && json_cat_P(json, PSTR(",\"host\":\""), len);
        ok = ok && json_cat_esc(json, _cfg.host, len);   // escapes in place
        ok = ok && json_cat_P(json, PSTR("\""), len);

        ok = ok && json_fitted(snprintf_P(buf, sizeof(buf),
                                          PSTR(",\"port\":%u,\"minSeverity\":%u,\"facility\":%u}"),
                                          (unsigned)_cfg.port, (unsigned)_cfg.minSeverity,
                                          (unsigned)_cfg.facility), sizeof(buf));
        ok = ok && json_cat(json, buf, len);

        _unlock();

        if (!ok) json[0] = '\0';   // empty string rather than half a document
        return ok;
    }

protected:
    SyslogConfig _cfg;

    // Lock: a real timed mutex exists only where there is preemptive
    // concurrency; AVR and ESP8266 need none. Same reasoning as MqttProfile.
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

    /// Storage layout. Virtual so a subclass can add its own fields; call the
    /// base first.
    virtual bool _doSave(Preferences& p) {
        bool ok = true;
        ok = ok && p.putBool("en", _cfg.enabled);
        ok = ok && prefs_putStr(p, "host", _cfg.host);
        ok = ok && (p.putUShort("port", _cfg.port) > 0);
        ok = ok && (p.putUChar("sev", _cfg.minSeverity) > 0);
        ok = ok && (p.putUChar("fac", _cfg.facility) > 0);
        return ok;
    }

    /// Counterpart of _doSave(). Every field is reset to its default before
    /// it is read, so what this leaves behind depends only on the store — see
    /// MqttProfile::_doLoad() for why the string needs it explicitly.
    virtual bool _doLoad(Preferences& p) {
        const SyslogConfig def{};
        _cfg.enabled = p.getBool("en", def.enabled);
        _cfg.host[0] = '\0';
        prefs_getStr(p, "host", _cfg.host, sizeof(_cfg.host));
        _cfg.port        = p.getUShort("port", def.port);
        _cfg.minSeverity = p.getUChar("sev", def.minSeverity);
        _cfg.facility    = p.getUChar("fac", def.facility);
        return true;
    }
};
