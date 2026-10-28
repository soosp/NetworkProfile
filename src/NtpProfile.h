#pragma once

/**
 * @file NtpProfile.h
 * @brief Stored NTP server addresses.
 *
 * A device has one clock, so the servers it synchronises from are a property of
 * the device, not of the interface it happens to be using. Holding them here
 * rather than in each NetworkProfile keeps one copy instead of one per
 * interface: nothing to type twice, nothing to store twice, and nothing for the
 * portal to repeat in the largest document it serves.
 *
 * Where interfaces genuinely reach different time servers — an internal one on a
 * wired LAN, a public pool over a hotspot — keep two NtpProfile instances and
 * hand the manager whichever applies. Two objects say that plainly; a field
 * buried in each interface's configuration said it by accident.
 *
 * Storage and validation only: this starts no client and knows nothing about the
 * protocol. NetworkManager applies the servers when an interface comes up.
 *
 * Costs nothing when unused. Declaring no instance means no buffers, and the
 * linker drops the code, so a project without NTP simply does not include this
 * header. Set NTP_PROFILE_ENABLED to 0 to leave NTP out of NetworkManager as
 * well — on AVR that is the default, since its SNTP client is a UDP socket and a
 * state machine rather than a buffer.
 *
 * Extending it follows the WiFiProfile pattern — override the virtual hooks:
 * @code
 * class MyNtpProfile : public NtpProfile {
 * public:
 *     struct Config : NtpProfile::NtpConfig { int16_t tzMinutes = 0; };
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

#include "NetworkProfileVersion.h"
#include "Host.h"
#include "detail/json_tools.h"
#include "detail/prefs_tools.h"

#include "detail/ntp_config.h"   // NTP_PROFILE_SERVER_COUNT and its defaults

/** @brief How long a bulk operation waits for the lock, in ms. */
#ifndef NTP_PROFILE_MUTEX_TIMEOUT
#  define NTP_PROFILE_MUTEX_TIMEOUT 1000
#endif

// Declared only where NTP exists in the build. NetworkManager's own SNTP
// support is guarded by the same switch, so the two cannot disagree.
#if NTP_PROFILE_ENABLED

class NtpProfile {
public:
    static constexpr uint32_t MUTEX_TIMEOUT = NTP_PROFILE_MUTEX_TIMEOUT;

    /** @brief Stored servers per profile; 1..3, see NTP_PROFILE_SERVER_COUNT. */
    static constexpr uint8_t SERVER_COUNT = NTP_PROFILE_SERVER_COUNT;

    /** @brief The stored settings. */
    struct NtpConfig {
        /// Each entry is an FQDN or an IP literal; empty means the slot is unused.
        /// Order is preserved as entered, so a slot may be empty with a later one
        /// filled — the manager compacts them when handing them to SNTP.
        char server[SERVER_COUNT][Host::MAX_FQDN_SIZE] = {};
    };

    /** @brief Result of checkConfig(). */
    enum class ConfigCheck : uint8_t {
        Ok = 0,     ///< Usable as it stands, including "no servers at all".
        BadServer   ///< An entry is neither a valid FQDN nor an IP literal.
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
            case ConfigCheck::Ok:        return F("ok");
            case ConfigCheck::BadServer: return F("invalid NTP server");
        }
        return F("unknown");
    }

    virtual ~NtpProfile() = default;

    /**
     * @brief Copies the stored servers out.
     * @return false if the lock could not be obtained; @p out is untouched.
     */
    bool getConfig(NtpConfig& out) const {
        if (!_lock()) return false;
        out = _cfg;
        _unlock();
        return true;
    }

    /**
     * @brief Replaces the stored servers.
     *
     * Validated as a whole through checkConfig(), so this refuses exactly what
     * setServer() refuses — an entry that is neither an FQDN nor an IP literal
     * — and for the same reason: a stored address the resolver will never
     * accept is a clock that silently never syncs. Reaching the two through the
     * same object should not decide whether the check happens.
     *
     * Empty slots stay valid. "No servers at all" is a configuration, not a
     * failure: it is how an interface is told to take the time server its DHCP
     * lease offers.
     *
     * Nothing is written unless the whole struct passes, so a rejected call
     * leaves the previous servers intact rather than half-replaced. Use
     * checkConfig() first where the reason matters; configCheckName() turns the
     * result into text.
     *
     * @return false if @p in was refused or the lock could not be obtained;
     *         either way nothing was changed.
     */
    bool setConfig(const NtpConfig& in) {
        if (checkConfig(in) != ConfigCheck::Ok) return false;
        if (!_lock()) return false;
        _cfg = in;
        _unlock();
        return true;
    }

    /**
     * @brief Reads one server.
     *
     * A buffer too small for the address gets what fits, and this still reports
     * true — a caller cannot tell a truncated host name from a whole one, and a
     * truncated one is a different server rather than a partial answer. Size
     * @p out at Host::MAX_FQDN_SIZE, which always holds whatever setServer()
     * accepted.
     *
     * @param index Slot, below SERVER_COUNT.
     * @param out   Filled with the address, or an empty string for an unused slot.
     * @param len   Size of @p out.
     * @return false on a bad index, a null or zero-length buffer, or a lock
     *         timeout. Truncation is not reported.
     */
    bool getServer(uint8_t index, char* out, size_t len) const {
        if (index >= SERVER_COUNT || !out || len == 0) return false;
        if (!_lock()) return false;
        snprintf(out, len, "%s", _cfg.server[index]);
        _unlock();
        return true;
    }

    /**
     * @brief Writes one server.
     * @param str   FQDN or IP literal; empty or nullptr clears the slot.
     * @param index Slot, below SERVER_COUNT.
     * @return false on a bad index, an address that is neither an FQDN nor an
     *         IP literal, or a lock timeout.
     */
    bool setServer(const char* str, uint8_t index = 0) {
        if (index >= SERVER_COUNT) return false;
        if (str && str[0] != '\0'
            && !Host::isValidIp(str) && !Host::isValidFqdn(str)) return false;
        if (!_lock()) return false;
        if (str) snprintf(_cfg.server[index], Host::MAX_FQDN_SIZE, "%s", str);
        else     _cfg.server[index][0] = '\0';
        _unlock();
        return true;
    }

    /** @brief True if at least one slot holds an address. */
    bool isConfigured() const {
        if (!_lock()) return false;
        bool any = false;
        for (uint8_t i = 0; i < SERVER_COUNT && !any; i++)
            any = (_cfg.server[i][0] != '\0');
        _unlock();
        return any;
    }

    /**
     * @brief Checks the settings for internal consistency.
     *
     * Having no servers at all passes: that is a device told to take its time
     * from DHCP, or from nowhere, and refusing to store it would be wrong.
     *
     * Takes the config as a parameter, like NetworkProfile::checkConfig(), so it
     * validates a staged copy and touches no member state. Virtual rather than
     * static, though — a static one called through a base reference silently
     * skips a subclass's own rules.
     *
     * @param cfg Settings to check.
     * @return ConfigCheck::Ok, or the first problem found.
     */
    virtual ConfigCheck checkConfig(const NtpConfig& cfg) const {
        for (uint8_t i = 0; i < SERVER_COUNT; i++) {
            const char* s = cfg.server[i];
            if (s[0] == '\0') continue;                    // unused slot
            if (!Host::isValidIp(s) && !Host::isValidFqdn(s))
                return ConfigCheck::BadServer;
        }
        return ConfigCheck::Ok;
    }

    /**
     * @brief Refuses a subclass's config, so the omission cannot be silent.
     *
     * checkConfig() is virtual, so a subclass's rules do run through a base
     * reference — but only if the *argument* keeps its type. Passing a derived
     * config where an NtpConfig is expected would slice it, and the subclass's
     * own fields would be gone before it could look at them.
     */
    template <class T, class = typename std::enable_if<
                  std::is_base_of<NtpConfig, T>::value &&
                  !std::is_same<NtpConfig, T>::value>::type>
    ConfigCheck checkConfig(const T&) const = delete;

    /** @copydoc checkConfig(const T&) */
    template <class T, class = typename std::enable_if<
                  std::is_base_of<NtpConfig, T>::value &&
                  !std::is_same<NtpConfig, T>::value>::type>
    bool setConfig(const T&) = delete;

    /**
     * @brief Writes the settings to @p ns.
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
     * @brief Erases the whole namespace @p ns.
     *
     * The counterpart of saveCfg(), and what a factory reset needs: without it
     * a device wiped of its network configuration would still come back holding
     * the time servers it used to synchronise from.
     *
     * Clears the store, not this object. The instance keeps whatever it holds
     * until something loads over it — construct fresh, or restart, to be sure
     * the two agree.
     *
     * @param ns Preferences namespace to erase.
     * @return true if the namespace was erased; false on a null namespace, a
     *         storage failure, or a lock timeout.
     */
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
     * @brief Reads servers stored per interface, under `ntp0`..`ntpN` in a
     *        network profile's own namespace.
     *
     * A device upgraded in the field would otherwise come back with no time
     * servers and no indication why. Reads them from that location once; save to
     * the new namespace afterwards.
     *
     * @param legacyNs Namespace a NetworkProfile was saved to, e.g. "net_eth".
     * @return true if at least one server was found.
     *
     * @deprecated Scheduled for removal in 1.0.0 — call it once from a firmware
     *             that upgrades existing devices, then drop it.
     */
    bool importLegacy(const char* legacyNs) {
        if (!legacyNs) return false;
        if (!_lock()) return false;
        Preferences p;
        if (!p.begin(legacyNs, true)) { _unlock(); return false; }
        bool found = false;
        for (uint8_t i = 0; i < SERVER_COUNT; i++) {
            char key[8];
            snprintf(key, sizeof(key), "ntp%u", (unsigned)i);
            char buf[Host::MAX_FQDN_SIZE] = {};
            prefs_getStr(p, key, buf, sizeof(buf));
            if (buf[0] != '\0') {
                snprintf(_cfg.server[i], Host::MAX_FQDN_SIZE, "%s", buf);
                found = true;
            }
        }
        p.end();
        _unlock();
        return found;
    }

    /**
     * @brief Renders the servers as a JSON object, for backup.
     * @param json Destination buffer, null-terminated on return.
     * @param len  Size of @p json.
     * @return true if the whole document fit.
     */
    virtual bool toJson(char* json, size_t len) const {
        if (!json || len == 0) return false;

        char key[16];
        json[0] = '\0';
        if (!_lock()) return false;

        bool ok = json_cat_P(json, PSTR("{"), len);
        for (uint8_t i = 0; i < SERVER_COUNT; i++) {
            ok = ok && json_fitted(snprintf_P(key, sizeof(key),
                            PSTR("%s\"ntp%u\":\""), i ? "," : "", (unsigned)i),
                            sizeof(key));
            ok = ok && json_cat(json, key, len);
            ok = ok && json_cat_esc(json, _cfg.server[i], len);
            ok = ok && json_cat_P(json, PSTR("\""), len);
        }
        ok = ok && json_cat_P(json, PSTR("}"), len);

        _unlock();

        if (!ok) json[0] = '\0';   // empty string rather than half a document
        return ok;
    }

protected:
    NtpConfig _cfg;

    /// Storage layout. Virtual so a subclass can add its own fields without
    /// restating these; call the base first. The key names match what
    /// NetworkProfile used, so importLegacy() and this agree.
    virtual bool _doSave(Preferences& p) {
        bool ok = true;
        for (uint8_t i = 0; i < SERVER_COUNT; i++) {
            char key[8];
            snprintf(key, sizeof(key), "ntp%u", (unsigned)i);
            ok = ok && prefs_putStr(p, key, _cfg.server[i]);
        }
        return ok;
    }

    /// Counterpart of _doSave(); a missing key leaves the slot empty.
    virtual bool _doLoad(Preferences& p) {
        for (uint8_t i = 0; i < SERVER_COUNT; i++) {
            char key[8];
            snprintf(key, sizeof(key), "ntp%u", (unsigned)i);
            _cfg.server[i][0] = '\0';
            prefs_getStr(p, key, _cfg.server[i], Host::MAX_FQDN_SIZE);
        }
        return true;
    }

    // Lock: a real timed mutex exists only where there is preemptive
    // concurrency (ESP32, and the host-test build). AVR is single-threaded and
    // ESP8266 schedules its callbacks cooperatively (never preempting loop()),
    // so on both the lock is a no-op. Same reasoning as NetworkProfile.
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
};

#endif  // NTP_PROFILE_ENABLED
