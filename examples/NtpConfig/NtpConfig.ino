/**
 * @file NtpConfig.ino
 * @brief NetworkProfile library — NTP server settings example (ESP32 / ESP8266)
 *
 * NtpProfile stores the servers a device synchronises its clock from. It holds
 * no client and knows nothing about the protocol: starting SNTP is
 * NetworkManager's job, and this is only what survives a reboot.
 *
 * The servers belong to the device rather than to an interface — one clock, one
 * set of servers, whichever way the traffic goes. Where interfaces genuinely
 * reach different time servers, the answer is a second instance rather than a
 * field per interface; the last section shows that.
 *
 * Demonstrates:
 *   - setServer() with an FQDN, a dotted-decimal IP, an invalid string, and ""
 *   - getServer() read-back, including empty slots, an out-of-range index and
 *     an undersized buffer
 *   - isConfigured() before and after clearing every slot
 *   - checkConfig(), and why "no servers at all" is a valid answer
 *   - getConfig() / setConfig() as an atomic snapshot
 *   - Save → load persistence
 *   - toJson() for backup
 *   - importLegacy(), for a device upgraded from a version that stored the
 *     servers per interface
 *   - Two instances, for the case one device-level profile does not cover
 *
 * Runs without a network: nothing here connects to anything.
 */

#include <Arduino.h>
#include <NtpProfile.h>

// Preferences namespace for the save/load round-trip.
static constexpr char NS_NTP[] = "net_ntp";

NtpProfile ntpProfile;

/// Prints every slot, so the effect of each step is visible rather than implied.
static void dumpServers(NtpProfile& p, const char* label) {
    Serial.printf("%s:\n", label);
    for (uint8_t i = 0; i < NtpProfile::SERVER_COUNT; i++) {
        char buf[Host::MAX_FQDN_SIZE];
        const bool ok = p.getServer(i, buf, sizeof(buf));
        Serial.printf("  [%u] %s \"%s\"\n", i, ok ? "ok   " : "false", buf);
    }
    Serial.printf("  isConfigured() -> %s\n", p.isConfigured() ? "true" : "false");
}

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    Serial.println("\n--- NetworkProfile NtpConfig ---");
    Serial.printf("SERVER_COUNT = %u\n\n", (unsigned)NtpProfile::SERVER_COUNT);

    // 1. setServer() — what is accepted and what is not
    //
    // An FQDN or a dotted-decimal IP; anything else is refused rather than
    // stored, so a typo cannot become a server nobody can reach.
    Serial.println("1. setServer()");
    Serial.printf("   \"pool.ntp.org\"     -> %s\n",
                  ntpProfile.setServer("pool.ntp.org", 0) ? "ok" : "rejected");
    if (NtpProfile::SERVER_COUNT >= 2) {
        Serial.printf("   \"216.239.35.0\"     -> %s\n",
                      ntpProfile.setServer("216.239.35.0", 1) ? "ok" : "rejected");
    }
    Serial.printf("   \"999.999.999.999\"  -> %s\n",
                  ntpProfile.setServer("999.999.999.999", 0) ? "ok" : "rejected (expected)");
    Serial.printf("   index out of range -> %s\n\n",
                  ntpProfile.setServer("pool.ntp.org", NtpProfile::SERVER_COUNT)
                      ? "ok" : "rejected (expected)");

    dumpServers(ntpProfile, "2. getServer() read-back");
    Serial.println();

    // 3. Undersized buffer
    //
    // The value is cut to fit rather than refused: the caller asked for as much
    // as would go in. Pass Host::MAX_FQDN_SIZE to get all of it.
    {
        char small[8];
        const bool ok = ntpProfile.getServer(0, small, sizeof(small));
        Serial.printf("3. getServer(0, small[8]) -> %s \"%s\"\n\n",
                      ok ? "true" : "false", small);
    }

    // 4. checkConfig() — validation of a staged copy
    //
    // Takes the configuration as a parameter, so nothing is touched until it
    // passes. No servers at all is *valid*: that is a device told to take its
    // time from DHCP, or from nowhere, and refusing to store it would be wrong.
    {
        NtpProfile::NtpConfig cfg;
        Serial.printf("4. checkConfig(empty) -> %s\n",
                      ntpProfile.checkConfig(cfg) == NtpProfile::ConfigCheck::Ok
                          ? "Ok (empty is a real answer)" : "BadServer");

        snprintf(cfg.server[0], sizeof(cfg.server[0]), "%s", "not a host name");
        Serial.printf("   checkConfig(bad)   -> %s\n\n",
                      ntpProfile.checkConfig(cfg) == NtpProfile::ConfigCheck::Ok
                          ? "Ok" : "BadServer (expected)");
    }

    // 5. getConfig() / setConfig() — the whole set at once
    //
    // One lock for the lot, and setConfig() replaces everything, so a
    // half-applied set cannot be observed. It validates the struct exactly as
    // setServer() validates one entry, so which of the two a caller reaches for
    // does not decide whether the check happens.
    {
        NtpProfile::NtpConfig cfg;
        if (ntpProfile.getConfig(cfg)) {
            snprintf(cfg.server[0], sizeof(cfg.server[0]), "%s", "0.pool.ntp.org");
            Serial.print("5. setConfig() -> ");
            if (ntpProfile.setConfig(cfg)) {
                Serial.println("ok");
            } else {
                Serial.print("rejected: ");
                Serial.println(
                    NtpProfile::configCheckName(ntpProfile.checkConfig(cfg)));
            }
        }
        dumpServers(ntpProfile, "   after setConfig()");
        Serial.println();
    }

    // 6. Persistence
    {
        Serial.printf("6. saveCfg(\"%s\") -> %s\n", NS_NTP,
                      ntpProfile.saveCfg(NS_NTP) ? "ok" : "failed");

        NtpProfile loaded;
        Serial.printf("   loadCfg()          -> %s\n",
                      loaded.loadCfg(NS_NTP) ? "ok" : "no record");
        dumpServers(loaded, "   loaded");
        Serial.println();
    }

    // 7. toJson() — for a backup file
    {
        char json[64 + NtpProfile::SERVER_COUNT * Host::MAX_FQDN_SIZE];
        Serial.printf("7. toJson() -> %s\n",
                      ntpProfile.toJson(json, sizeof(json)) ? "ok" : "did not fit");
        Serial.printf("   %s\n\n", json);
    }

    // 8. importLegacy() — upgrading a device in the field
    //
    // Servers stored per interface, under ntp0..ntpN in a network profile's own
    // namespace, are read from there once. A device flashed with this version
    // would otherwise come back with no time servers and no indication why.
    //
    // Scheduled for removal in 1.0.0: call it from the firmware that performs
    // the upgrade, then drop it.
    {
        NtpProfile migrated;
        const bool found = migrated.importLegacy("net_eth");
        Serial.printf("8. importLegacy(\"net_eth\") -> %s\n",
                      found ? "found servers" : "nothing there (expected on a fresh device)");
        if (found) migrated.saveCfg(NS_NTP);
        Serial.println();
    }

    // 9. Two profiles, for interfaces that really do differ
    //
    // The usual case is one: the same servers are reachable whichever way the
    // device is connected. But an internal time server on a wired LAN may be
    // unreachable over a phone hotspot, and then two instances say so plainly —
    // where a field inside each interface's configuration said it by accident,
    // and mostly said it twice with the same value.
    //
    // NetworkManager is handed whichever applies, from its CONNECTED handler:
    // that takes effect for the handover in progress, because events reach the
    // application earlier in the same update() pass than NTP is configured.
    {
        NtpProfile ethNtp, wifiNtp;
        ethNtp.setServer("192.168.1.1", 0);          // the LAN's own server
        wifiNtp.setServer("pool.ntp.org", 0);        // reachable from anywhere

        char a[Host::MAX_FQDN_SIZE], b[Host::MAX_FQDN_SIZE];
        ethNtp.getServer(0, a, sizeof(a));
        wifiNtp.getServer(0, b, sizeof(b));
        Serial.printf("9. two profiles: wired \"%s\", wireless \"%s\"\n\n", a, b);
    }

    // 10. Clearing every slot
    //
    // An empty string clears a slot, and with none left isConfigured() says so.
    // That state is stored and loaded like any other: "take NTP from DHCP" is a
    // decision, not the absence of one.
    for (uint8_t i = 0; i < NtpProfile::SERVER_COUNT; i++) ntpProfile.setServer("", i);
    dumpServers(ntpProfile, "10. after clearing every slot");

    Serial.println("\n--- done ---");
}

void loop() {
    delay(1000);
}
