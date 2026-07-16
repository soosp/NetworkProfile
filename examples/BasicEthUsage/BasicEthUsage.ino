/**
 * @file BasicEthUsage.ino
 * @brief NetworkProfile library — basic Ethernet only usage example
 *
 * Demonstrates:
 *   - Creating Ethernet profile
 *   - Setting static IP
 *   - Saving and loading profile via Preferences
 *   - Atomic configuration update via ethConfig struct
 *   - JSON serialisation
 *   - MAC address handling
 */

// Define a custom MAC
#define NETWORK_PROFILE_DEFAULT_MAC "4E:52:47:30:30:31"

// Only one NTP server - useful on AVR platform with weak resources
#define NETWORK_PROFILE_NTP_SERVER_COUNT 1

#include <Arduino.h>
#include <EthProfile.h>

// Preferences namespaces — one per interface
static constexpr char NS_ETH[]  = "net_eth";

EthProfile  ethProfile;

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    Serial.println("\n--- NetworkProfile BasicEthUsage ---");

    // -------------------------------------------------------------------------
    // 1. ETH profile — static IP
    // -------------------------------------------------------------------------
    NetworkProfile::NetworkConfig ethCfg;
    ethCfg.dhcp     = false;
    ethCfg.priority = 0;
    ethCfg.ip       = IPAddress(192, 168, 1, 100);
    ethCfg.mask     = IPAddress(255, 255, 255, 0);
    ethCfg.gateway  = IPAddress(192, 168, 1, 1);
    ethCfg.dns[0]   = IPAddress(8, 8, 8, 8);
    ethCfg.dns[1]   = IPAddress(8, 8, 4, 4);
    strncpy(ethCfg.ntp[0], "pool.ntp.org",   Host::MAX_FQDN_LEN);

    if (ethProfile.setConfig(ethCfg)) {
        Serial.println("ETH profile configured (static IP)");
    }

    // -------------------------------------------------------------------------
    // 2. Save profile to Preferences
    // -------------------------------------------------------------------------
    if (ethProfile.saveCfg(NS_ETH)) {
        Serial.println("ETH profile saved");
    }

    // -------------------------------------------------------------------------
    // 3. Load profiles back
    // -------------------------------------------------------------------------
    EthProfile ethLoaded;
    if (ethLoaded.loadCfg(NS_ETH)) {
        Serial.print("ETH profile loaded — IP: "); Serial.println(ethLoaded.getIp());
    }

    // -------------------------------------------------------------------------
    // 4. MAC address
    // -------------------------------------------------------------------------
    NetworkProfile::MACAddress mac;
    char macStr[NetworkProfile::MAC_STR_SIZE];

    // Default (hardware) MAC
    ethProfile.getMac(mac, NetworkProfile::ConfigSource::FACTORY);
    NetworkProfile::macToStr(macStr, sizeof(macStr), mac);
    Serial.print("ETH default MAC: "); Serial.println(macStr);

    // Set a custom MAC override
    NetworkProfile::MACAddress customMac = {0x4E, 0x52, 0x47, 0x00, 0x01, 0x02};
    if (ethProfile.setMac(customMac)) {
        ethProfile.getMac(mac);  // ACTIVE — returns override
        NetworkProfile::macToStr(macStr, sizeof(macStr), mac);
        Serial.print("ETH active MAC (override): "); Serial.println(macStr);
    }

    // -------------------------------------------------------------------------
    // 5. Hostname — active or default (MAC-based)
    // -------------------------------------------------------------------------
    char hostname[NetworkProfile::MAX_HOSTNAME_SIZE];
    ethProfile.getHostname(hostname, sizeof(hostname));
    Serial.print("ETH hostname (active): "); Serial.println(hostname);

    ethProfile.getHostname(hostname, sizeof(hostname),
                            NetworkProfile::ConfigSource::FACTORY);
    Serial.print("ETH hostname (default): "); Serial.println(hostname);

    // -------------------------------------------------------------------------
    // 6. JSON serialisation
    // -------------------------------------------------------------------------
    char json[NetworkProfile::JSON_SIZE];
    if (ethProfile.toJson(json, sizeof(json))) {
        Serial.print("ETH JSON:\n"); Serial.println(json);
    }

    // -------------------------------------------------------------------------
    // 7. Factory reset (clear saved data)
    // -------------------------------------------------------------------------
    // ethProfile.clearCfg(NS_ETH);    // Uncomment to clear ETH profile
}

void loop() {
    // The NetworkManager (separate library) would use these profiles
    // to apply configuration to the hardware interfaces:
    //
    //   networkManager.apply(ethProfile);   // Apply ETH config
}
