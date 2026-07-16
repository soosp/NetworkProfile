/**
 * @file NtpConfig.ino
 * @brief NetworkProfile library — NTP configuration example (ESP32 / ESP8266)
 *
 * Focuses on the NTP server API surface, complementing BasicUsage (which
 * configures NTP only through the NetworkConfig struct). Demonstrates:
 *   - setNtp() with an FQDN, a dotted-decimal IP, an invalid string, and ""
 *   - getNtp() read-back, including empty slots, out-of-range index, and
 *     truncation into an undersized buffer
 *   - isConfiguredNtp() before and after clearing every slot
 *   - getConfig() atomic snapshot of the NTP entries
 *   - Save → load → getNtp() persistence round-trip
 *   - JSON serialisation of the NTP fields
 *
 * NTP servers are stored as plain strings (validated as IP or FQDN); name
 * resolution is the SNTP client's job, handled by NetworkManager — not here.
 */

#include <Arduino.h>
#include <WiFiProfile.h>

// Preferences namespace for the save/load round-trip.
static constexpr char NS_NTP[] = "net_ntp";

WiFiProfile ntpProfile;

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    Serial.println("\n--- NetworkProfile NtpConfig ---");

#if (NETWORK_PROFILE_NTP_SERVER_COUNT > 0)
    char buf[Host::MAX_FQDN_SIZE];

    // -------------------------------------------------------------------------
    // 1. setNtp() — accepted and rejected inputs
    // -------------------------------------------------------------------------
    // FQDN in slot 0, dotted-decimal IP is also valid.
    Serial.printf("setNtp(0, \"pool.ntp.org\")   -> %s\n",
                  ntpProfile.setNtp("pool.ntp.org", 0) ? "ok" : "rejected");

#if (NETWORK_PROFILE_NTP_SERVER_COUNT >= 2)
    Serial.printf("setNtp(1, \"216.239.35.0\")   -> %s\n",
                  ntpProfile.setNtp("216.239.35.0", 1) ? "ok" : "rejected");
#endif

    // Neither a valid IP nor a valid FQDN — left unchanged, returns false.
    Serial.printf("setNtp(0, \"999.999.999.999\")-> %s\n",
                  ntpProfile.setNtp("999.999.999.999", 0) ? "ok" : "rejected (expected)");

    // -------------------------------------------------------------------------
    // 2. getNtp() — read-back of configured slots
    // -------------------------------------------------------------------------
    for (uint8_t i = 0; i < NetworkProfile::NTP_SERVER_COUNT; i++) {
        bool ok = ntpProfile.getNtp(i, buf, sizeof(buf));
        Serial.printf("getNtp(%u) -> %s \"%s\"\n", i, ok ? "true " : "false", buf);
    }

    // -------------------------------------------------------------------------
    // 3. getNtp() — edge cases
    // -------------------------------------------------------------------------
    // Out-of-range index: returns false, buffer set to "".
    bool oorOk = ntpProfile.getNtp(NetworkProfile::NTP_SERVER_COUNT, buf, sizeof(buf));
    Serial.printf("getNtp(out-of-range) -> %s \"%s\"\n",
                  oorOk ? "true" : "false (expected)", buf);

    // Truncation: an undersized buffer returns false but stays NUL-terminated.
    char small[5];
    bool truncOk = ntpProfile.getNtp(0, small, sizeof(small));
    Serial.printf("getNtp(0, small[5]) -> %s \"%s\"\n",
                  truncOk ? "true" : "false (expected)", small);

    // -------------------------------------------------------------------------
    // 4. isConfiguredNtp() — true while any slot is set
    // -------------------------------------------------------------------------
    Serial.printf("isConfiguredNtp() -> %s\n",
                  ntpProfile.isConfiguredNtp() ? "true" : "false");

    // -------------------------------------------------------------------------
    // 5. getConfig() — atomic snapshot of the NTP entries
    // -------------------------------------------------------------------------
    WiFiProfile::WiFiConfig cfg;
    if (ntpProfile.getConfig(cfg)) {
        for (uint8_t i = 0; i < NetworkProfile::NTP_SERVER_COUNT; i++) {
            Serial.printf("cfg.ntp[%u] = \"%s\"\n", i, cfg.ntp[i]);
        }
    }

    // -------------------------------------------------------------------------
    // 6. Persistence round-trip — save, load into a fresh profile, read back
    // -------------------------------------------------------------------------
    if (ntpProfile.saveCfg(NS_NTP)) {
        Serial.println("profile saved");
    }

    WiFiProfile loaded;
    if (loaded.loadCfg(NS_NTP)) {
        loaded.getNtp(0, buf, sizeof(buf));
        Serial.printf("loaded getNtp(0) -> \"%s\"\n", buf);
    }

    // -------------------------------------------------------------------------
    // 7. JSON serialisation (includes the ntp<N> fields)
    // -------------------------------------------------------------------------
    char json[NetworkProfile::JSON_SIZE];
    if (ntpProfile.toJson(json, sizeof(json))) {
        Serial.printf("JSON:\n%s\n", json);
    }

    // -------------------------------------------------------------------------
    // 8. Empty string clears a slot — isConfiguredNtp() then reflects it
    // -------------------------------------------------------------------------
    for (uint8_t i = 0; i < NetworkProfile::NTP_SERVER_COUNT; i++) {
        ntpProfile.setNtp("", i);
    }
    Serial.printf("after clearing all slots, isConfiguredNtp() -> %s\n",
                  ntpProfile.isConfiguredNtp() ? "true" : "false (expected)");

    // ntpProfile.clearCfg(NS_NTP);  // Uncomment to remove the saved data
#else
    Serial.println("NTP disabled (NETWORK_PROFILE_NTP_SERVER_COUNT == 0).");
    Serial.println("Rebuild with a non-zero server count to run this example.");
#endif
}

void loop() {
    // Applying these NTP servers to the SNTP client is the NetworkManager
    // library's job; NetworkProfile only stores and validates them.
}
