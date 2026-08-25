/**
 * @file MqttConfig.ino
 * @brief NetworkProfile library — MQTT broker settings example (ESP32 / ESP8266)
 *
 * MqttProfile stores what a device needs in order to reach a broker: whether
 * MQTT is wanted at all, the address, the port, TLS, and the credentials. It
 * contains no MQTT client and knows nothing about topics — connecting is the
 * application's job, and this is only what survives a reboot. Demonstrates:
 *   - setConfig() / getConfig() round-trip
 *   - checkConfig() rejecting each kind of bad input, and why `enabled` is a
 *     field of its own
 *   - Save → load persistence, including an empty password clearing the key
 *   - toJson() for backup, and what it deliberately leaves out
 *   - Subclassing to carry a project-specific field
 *
 * Runs without a broker or a network: nothing here connects to anything.
 */

#include <Arduino.h>
#include <MqttProfile.h>

// Preferences namespace for the save/load round-trip.
static constexpr char NS_MQTT[] = "mqtt";

MqttProfile mqtt;

/// Prints a ConfigCheck as text, so the output says what failed rather than
/// which enumerator index it was.
static const char* checkStr(MqttProfile::ConfigCheck c) {
    switch (c) {
        case MqttProfile::ConfigCheck::Ok:                  return "Ok";
        case MqttProfile::ConfigCheck::NoHost:              return "NoHost";
        case MqttProfile::ConfigCheck::BadHost:             return "BadHost";
        case MqttProfile::ConfigCheck::BadPort:             return "BadPort";
        case MqttProfile::ConfigCheck::PasswordWithoutUser: return "PasswordWithoutUser";
    }
    return "?";
}

// -----------------------------------------------------------------------------
// Extending the profile: a project that needs one more stored setting adds it
// in a subclass rather than in the library. Home Assistant discovery is a good
// example — useful to several projects, meaningless to the rest.
//
// checkConfig() is virtual, so the subclass's rules run even through a
// MqttProfile reference. Note that it takes the *derived* config type: handing
// a derived config to the base overload would slice it, which the library turns
// into a compile error rather than a silent half-check.
// -----------------------------------------------------------------------------
class HaMqttProfile : public MqttProfile {
public:
    struct HaConfig : public MqttConfig {
        bool discovery = false;   ///< publish Home Assistant discovery records
    };

    using MqttProfile::checkConfig;

    ConfigCheck checkConfig(const HaConfig& cfg) const {
        // Discovery without a broker to publish it to is a configuration error
        // worth catching here rather than at runtime.
        if (cfg.discovery && !cfg.enabled) return ConfigCheck::NoHost;
        return MqttProfile::checkConfig(static_cast<const MqttConfig&>(cfg));
    }
};

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println(F("\nMqttProfile example"));

    // -------------------------------------------------------------------------
    // 1. Defaults: disabled, no broker, plain-MQTT port
    // -------------------------------------------------------------------------
    MqttProfile::MqttConfig cfg;
    mqtt.getConfig(cfg);
    Serial.printf("defaults: enabled=%s host=\"%s\" port=%u tls=%s\n",
                  cfg.enabled ? "true" : "false", cfg.host,
                  (unsigned)cfg.port, cfg.tls ? "true" : "false");

    // -------------------------------------------------------------------------
    // 2. checkConfig() on the ways a form can be wrong
    //
    // A disabled profile always passes: its fields are not going to be used, and
    // refusing to save a half-filled form the user has just switched off would
    // only be in the way.
    // -------------------------------------------------------------------------
    Serial.printf("disabled, empty            -> %s\n", checkStr(mqtt.checkConfig(cfg)));

    cfg.enabled = true;
    Serial.printf("enabled, no host           -> %s\n", checkStr(mqtt.checkConfig(cfg)));

    snprintf(cfg.host, sizeof(cfg.host), "not a host name");
    Serial.printf("enabled, malformed host    -> %s\n", checkStr(mqtt.checkConfig(cfg)));

    // Either an FQDN or an IP literal is accepted, as for the NTP servers.
    snprintf(cfg.host, sizeof(cfg.host), "broker.example.org");
    cfg.port = 0;
    Serial.printf("port 0                     -> %s\n", checkStr(mqtt.checkConfig(cfg)));

    cfg.port = MqttProfile::DEFAULT_PORT;
    snprintf(cfg.password, sizeof(cfg.password), "s3cret");
    Serial.printf("password without username  -> %s\n", checkStr(mqtt.checkConfig(cfg)));

    snprintf(cfg.user, sizeof(cfg.user), "sensor01");
    Serial.printf("complete                   -> %s\n", checkStr(mqtt.checkConfig(cfg)));

    // -------------------------------------------------------------------------
    // 3. TLS uses a different well-known port, but changing one does not change
    //    the other: the port is whatever was configured, and guessing would
    //    overwrite a deliberate choice.
    // -------------------------------------------------------------------------
    cfg.tls  = true;
    cfg.port = MqttProfile::DEFAULT_TLS_PORT;
    if (!mqtt.setConfig(cfg)) {
        Serial.print(F("setConfig rejected: "));
        Serial.println(MqttProfile::configCheckName(mqtt.checkConfig(cfg)));
    }

    // -------------------------------------------------------------------------
    // 4. Persistence round-trip
    // -------------------------------------------------------------------------
    if (mqtt.saveCfg(NS_MQTT)) {
        MqttProfile fresh;
        if (fresh.loadCfg(NS_MQTT)) {
            MqttProfile::MqttConfig back;
            fresh.getConfig(back);
            Serial.printf("loaded: %s:%u tls=%s user=\"%s\" password=%s\n",
                          back.host, (unsigned)back.port,
                          back.tls ? "true" : "false", back.user,
                          back.password[0] ? "(stored)" : "(empty)");
        } else {
            Serial.println(F("load failed — nothing saved yet?"));
        }
    }

    // -------------------------------------------------------------------------
    // 5. An empty password removes the stored one rather than storing a blank,
    //    so a later load falls back to the default instead of an empty string.
    // -------------------------------------------------------------------------
    cfg.password[0] = '\0';
    if (!mqtt.setConfig(cfg)) {
        Serial.print(F("setConfig rejected: "));
        Serial.println(MqttProfile::configCheckName(mqtt.checkConfig(cfg)));
    }
    mqtt.saveCfg(NS_MQTT);

    // -------------------------------------------------------------------------
    // 6. Backup JSON. The password is absent by design: a backup file travels
    //    through mail and cloud storage, and a broker credential in it is a
    //    credential leaked. Restoring one therefore needs it typed again.
    // -------------------------------------------------------------------------
    char json[256];
    if (mqtt.toJson(json, sizeof(json))) Serial.printf("JSON:\n%s\n", json);

    // -------------------------------------------------------------------------
    // 7. The subclass, checked through a base reference
    // -------------------------------------------------------------------------
    HaMqttProfile          ha;
    HaMqttProfile::HaConfig haCfg;
    haCfg.enabled   = false;
    haCfg.discovery = true;            // discovery, but MQTT switched off
    Serial.printf("HA discovery while disabled -> %s\n", checkStr(ha.checkConfig(haCfg)));

    // mqtt.saveCfg(NS_MQTT);  // the settings above are already stored
}

void loop() {
    // Connecting, subscribing and publishing are the application's job:
    // MqttProfile only stores and validates. See the AsyncConfigPortal
    // MqttConfigComponent for editing these settings from a web page.
}
