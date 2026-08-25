#pragma once

/**
 * @file MqttProfile.h
 * @brief Stored MQTT broker settings: address, port, TLS, credentials.
 *
 * Sits in this library for the same reason the NTP servers do: it is something a
 * networked device stores in order to reach a service, and splitting it into its
 * own library would separate things that are always deployed together. It knows
 * nothing about MQTT itself — no client, no topics, no connection. The
 * application owns those; this owns what survives a reboot.
 *
 * Topics are deliberately absent. A project derives them from its own naming
 * (project name, hostname, sensor index), and a profile that guessed at a topic
 * layout would be wrong for every second project. Add them in a subclass where
 * they are project-specific enough to configure.
 *
 * Extending it follows the WiFiProfile pattern — override the virtual hooks:
 * @code
 * class TemperatureMonitorMqttProfile : public MqttProfile {
 * public:
 *     struct Config : MqttProfile::MqttConfig { bool haDiscovery = false; };
 *     // override _doSave/_doLoad/checkConfig/toJson to carry the extra field
 * };
 * @endcode
 */

#include <Arduino.h>
#include <type_traits>

#if !defined(ARDUINO_ARCH_AVR) && !defined(ARDUINO_ARCH_ESP8266)
#  include <mutex>
#  include <chrono>
#endif

#include "Host.h"
#include "detail/json_tools.h"
#include "detail/prefs_tools.h"

/** @brief How long a bulk operation waits for the lock, in ms. */
#ifndef MQTT_PROFILE_MUTEX_TIMEOUT
#  define MQTT_PROFILE_MUTEX_TIMEOUT 1000
#endif

/** @brief Maximum length of an MQTT username, excluding the terminator. */
#ifndef MQTT_PROFILE_MAX_USER_LEN
#  define MQTT_PROFILE_MAX_USER_LEN 32
#endif

/** @brief Maximum length of an MQTT password, excluding the terminator. */
#ifndef MQTT_PROFILE_MAX_PASSWORD_LEN
#  define MQTT_PROFILE_MAX_PASSWORD_LEN 64
#endif

class MqttProfile {
public:
    static constexpr uint32_t MUTEX_TIMEOUT   = MQTT_PROFILE_MUTEX_TIMEOUT;

    static constexpr size_t MAX_USER_LEN      = MQTT_PROFILE_MAX_USER_LEN;
    static constexpr size_t MAX_USER_SIZE     = MAX_USER_LEN + 1;
    static constexpr size_t MAX_PASSWORD_LEN  = MQTT_PROFILE_MAX_PASSWORD_LEN;
    static constexpr size_t MAX_PASSWORD_SIZE = MAX_PASSWORD_LEN + 1;

    static constexpr uint16_t DEFAULT_PORT     = 1883;   ///< plain MQTT
    static constexpr uint16_t DEFAULT_TLS_PORT = 8883;   ///< MQTT over TLS

    /** @brief The stored settings. */
    struct MqttConfig {
        /**
         * Whether the application should connect at all. Separate from an empty
         * host on purpose: "switched off" and "not configured yet" are two
         * different facts, and a single field cannot carry both — a device
         * temporarily taken off the broker would otherwise have to forget its
         * address to do it.
         */
        bool     enabled = false;

        char     host[Host::MAX_FQDN_SIZE] = {};  ///< Broker: FQDN or IP literal.
        uint16_t port    = DEFAULT_PORT;
        bool     tls     = false;

        /// Empty username means an anonymous connection.
        char     user[MAX_USER_SIZE] = {};
        char     password[MAX_PASSWORD_SIZE] = {};
    };

    /** @brief Result of checkConfig(). */
    enum class ConfigCheck : uint8_t {
        Ok = 0,             ///< Usable as it stands.
        NoHost,             ///< Enabled but no broker address.
        BadHost,            ///< Not a valid FQDN or IP literal.
        BadPort,            ///< Port is zero.
        PasswordWithoutUser ///< A password with no username to go with it.
    };

    /**
     * @brief Names a ConfigCheck value, for logs and configuration UIs.
     *
     * Counterpart of NetworkProfile::configCheckName() for this class's own
     * enum — the two are separate types, so a caller needs whichever belongs to
     * the profile it asked. Flash-resident, so printing costs no RAM on AVR.
     *
     * @param c Value to name.
     * @return Flash-resident string, printable with Serial.print().
     */
    static const __FlashStringHelper* configCheckName(ConfigCheck c) {
        switch (c) {
            case ConfigCheck::Ok:                  return F("ok");
            case ConfigCheck::NoHost:              return F("no broker address");
            case ConfigCheck::BadHost:             return F("invalid broker address");
            case ConfigCheck::BadPort:             return F("invalid port");
            case ConfigCheck::PasswordWithoutUser: return F("password without username");
        }
        return F("unknown");
    }

    virtual ~MqttProfile() = default;

    /**
     * @brief Copies the current settings out.
     *
     * Locked: the struct is copied field by field, so a write landing in the
     * middle of it — the portal saving while the client task reads — would hand
     * out half of each. @p out is left untouched if the lock is not obtained.
     *
     * @param out Filled on success.
     * @return true if the settings were copied.
     */
    bool getConfig(MqttConfig& out) const {
        if (!_lock()) return false;
        out = _cfg;
        _unlock();
        return true;
    }

    /**
     * @brief Replaces the current settings.
     *
     * Validated as a whole through checkConfig(), so a broker address the
     * resolver will never accept cannot reach storage. The portal pattern is
     * unchanged — stage a config, check it, keep it — this only means the last
     * step no longer depends on the first having been done.
     *
     * A disabled profile still passes whatever its other fields hold, so a
     * half-filled form the user has switched off is kept, as before.
     *
     * Nothing is written unless the whole struct passes, so a rejected call
     * leaves the previous settings intact rather than half-replaced. Use
     * checkConfig() first where the reason matters; configCheckName() turns the
     * result into text.
     *
     * @return false if @p in was refused or the lock could not be obtained;
     *         either way nothing was changed.
     */
    bool setConfig(const MqttConfig& in) {
        if (checkConfig(in) != ConfigCheck::Ok) return false;
        if (!_lock()) return false;
        _cfg = in;
        _unlock();
        return true;
    }

    /**
     * @brief Refuses a subclass's config, so the omission cannot be silent.
     *
     * checkConfig() is virtual, so a subclass's rules do run through a base
     * reference — but only if the *argument* keeps its type. Passing a derived
     * config where a MqttConfig is expected would slice it, and the subclass's
     * own fields would be gone before it could look at them. Deleting the
     * derived-type overload makes that a compile error; a subclass declares its
     * own overload taking its own config type.
     */
    template <class T, class = typename std::enable_if<
                  std::is_base_of<MqttConfig, T>::value &&
                  !std::is_same<MqttConfig, T>::value>::type>
    ConfigCheck checkConfig(const T&) const = delete;

    /** @copydoc checkConfig(const T&) */
    template <class T, class = typename std::enable_if<
                  std::is_base_of<MqttConfig, T>::value &&
                  !std::is_same<MqttConfig, T>::value>::type>
    bool setConfig(const T&) = delete;

    /**
     * @brief Checks the settings for internal consistency.
     *
     * Only what can be decided without a network: whether the broker will accept
     * these credentials is not knowable here.
     *
     * A disabled profile always passes — its fields are not going to be used,
     * and refusing to save a half-filled form the user has just switched off
     * would be unhelpful.
     *
     * Takes the config as a parameter, like NetworkProfile::checkConfig(): a
     * caller validates a staged copy before deciding to keep it, and touching no
     * member state means there is nothing to lock. Virtual rather than static,
     * though — a static one called through a base reference silently skips a
     * subclass's own rules.
     *
     * @param cfg Settings to check.
     * @return ConfigCheck::Ok, or the first problem found.
     */
    virtual ConfigCheck checkConfig(const MqttConfig& cfg) const {
        if (!cfg.enabled) return ConfigCheck::Ok;
        if (cfg.host[0] == '\0') return ConfigCheck::NoHost;
        if (!Host::isValidIp(cfg.host) && !Host::isValidFqdn(cfg.host))
            return ConfigCheck::BadHost;
        if (cfg.port == 0) return ConfigCheck::BadPort;
        if (cfg.password[0] != '\0' && cfg.user[0] == '\0')
            return ConfigCheck::PasswordWithoutUser;
        return ConfigCheck::Ok;
    }

    /**
     * @brief Writes the settings to @p ns.
     *
     * The namespace is the caller's, so one device can keep several brokers'
     * settings side by side.
     *
     * @param ns Preferences namespace.
     * @return true on success.
     */
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

    /**
     * @brief Reads the settings from @p ns.
     * @param ns Preferences namespace.
     * @return true if a complete record was found; false leaves the defaults.
     */
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

    /**
     * @brief Renders the settings as a JSON object, for backup.
     *
     * The password is left out, as it is everywhere else: a backup file travels
     * through mail and cloud drives, and a broker credential in it is a
     * credential leaked. Restoring therefore needs the password entered again,
     * which is the same trade the Wi-Fi profile makes.
     *
     * @param json Destination buffer, null-terminated on return.
     * @param len  Size of @p json.
     * @return true if the whole document fit.
     */
    virtual bool toJson(char* json, size_t len) const {
        if (!json || len == 0) return false;

        char buf[32];
        json[0] = '\0';
        if (!_lock()) return false;

        bool ok = json_cat_P(json, PSTR("{\"enabled\":"), len);
        ok = ok && json_cat_P(json, _cfg.enabled ? PSTR("true") : PSTR("false"), len);

        ok = ok && json_cat_P(json, PSTR(",\"host\":\""), len);
        ok = ok && json_cat_esc(json, _cfg.host, len);   // escapes in place
        ok = ok && json_cat_P(json, PSTR("\""), len);

        ok = ok && json_fitted(snprintf_P(buf, sizeof(buf), PSTR(",\"port\":%u"),
                                          (unsigned)_cfg.port), sizeof(buf));
        ok = ok && json_cat(json, buf, len);

        ok = ok && json_cat_P(json, PSTR(",\"tls\":"), len);
        ok = ok && json_cat_P(json, _cfg.tls ? PSTR("true") : PSTR("false"), len);

        ok = ok && json_cat_P(json, PSTR(",\"user\":\""), len);
        ok = ok && json_cat_esc(json, _cfg.user, len);
        ok = ok && json_cat_P(json, PSTR("\"}"), len);

        _unlock();

        if (!ok) json[0] = '\0';   // empty string rather than half a document
        return ok;
    }

protected:
    MqttConfig _cfg;

    // Lock: a real timed mutex exists only where there is preemptive
    // concurrency (ESP32, and the host-test build). AVR is single-threaded and
    // ESP8266 schedules its callbacks cooperatively (never preempting loop()),
    // so on both the lock is a no-op. Same reasoning, and same timeout knob, as
    // NetworkProfile.
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


    /// Storage layout. Virtual so a subclass can add its own fields without
    /// restating these; call the base first.
    virtual bool _doSave(Preferences& p) {
        bool ok = true;
        ok = ok && p.putBool("en", _cfg.enabled);
        ok = ok && prefs_putStr(p, "host", _cfg.host);
        ok = ok && (p.putUShort("port", _cfg.port) > 0);
        ok = ok && p.putBool("tls", _cfg.tls);
        ok = ok && prefs_putStr(p, "user", _cfg.user);
        ok = ok && prefs_putStr(p, "pass", _cfg.password);
        return ok;
    }

    /**
     * Counterpart of _doSave(). Every field is reset to its MqttConfig default
     * before it is read, so what this leaves behind depends only on the store.
     * The string fields need it explicitly: prefs_putStr() removes the key of
     * an empty value and Preferences::getString() leaves the destination alone
     * when the key is absent, so clearing the broker address, saving, then
     * loading back into the same object would have restored the old one. The
     * bools and the port pass their default as the getter's argument, which
     * does the same job.
     */
    virtual bool _doLoad(Preferences& p) {
        const MqttConfig def{};
        _cfg.enabled = p.getBool("en", def.enabled);
        _cfg.host[0] = '\0';
        p.getString("host", _cfg.host, sizeof(_cfg.host));
        _cfg.port    = p.getUShort("port", def.port);
        _cfg.tls     = p.getBool("tls", def.tls);
        _cfg.user[0]     = '\0';
        _cfg.password[0] = '\0';
        p.getString("user", _cfg.user, sizeof(_cfg.user));
        p.getString("pass", _cfg.password, sizeof(_cfg.password));
        return true;
    }
};
