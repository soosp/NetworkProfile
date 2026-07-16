/**
 * @file BasicUsage.ino
 * @brief NetworkProfile library — basic usage example (ESP32)
 *
 * Demonstrates:
 *   - Creating WiFi and Ethernet profiles
 *   - Setting static IP and DHCP configuration
 *   - Saving and loading profiles via Preferences
 *   - Atomic configuration update via WiFiConfig struct
 *   - JSON serialisation
 *   - MAC address handling
 *   - Interface priority for ETH → WiFi fallback
 */

#include <Arduino.h>
#include "WiFiProfile.h"
#include "EthProfile.h"

// Preferences namespaces — one per interface
static constexpr char NS_WIFI[] = "net_wifi";
static constexpr char NS_ETH[]  = "net_eth";

WiFiProfile wifiProfile;
EthProfile  ethProfile;

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    Serial.println("\n--- NetworkProfile BasicUsage ---");

    // -------------------------------------------------------------------------
    // 1. WiFi profile — DHCP with credentials
    // -------------------------------------------------------------------------
    WiFiProfile::WiFiConfig wifiCfg;
    wifiCfg.dhcp     = true;
    wifiCfg.priority = 1;                    // Lower priority than ETH
    strncpy(wifiCfg.ssid,     "MyNetwork",  MAX_SSID_LEN);
    strncpy(wifiCfg.password, "MyPassword", WiFiProfile::MAX_PASSWORD_LEN);
    strncpy(wifiCfg.hostname, "esp32-node", NetworkProfile::MAX_HOSTNAME_LEN);
    strncpy(wifiCfg.ntp[0],   "pool.ntp.org", Host::MAX_FQDN_LEN);

    if (wifiProfile.setConfig(wifiCfg)) {
        Serial.println("WiFi profile configured (DHCP)");
    }

    // -------------------------------------------------------------------------
    // 2. ETH profile — static IP, higher priority
    // -------------------------------------------------------------------------
    NetworkProfile::NetworkConfig ethCfg;
    ethCfg.dhcp     = false;
    ethCfg.priority = 0;                     // Higher priority than WiFi
    ethCfg.ip       = IPAddress(192, 168, 1, 100);
    ethCfg.mask     = IPAddress(255, 255, 255, 0);
    ethCfg.gateway  = IPAddress(192, 168, 1, 1);
    ethCfg.dns[0]   = IPAddress(8, 8, 8, 8);
    ethCfg.dns[1]   = IPAddress(8, 8, 4, 4);
    strncpy(ethCfg.ntp[0], "pool.ntp.org",   Host::MAX_FQDN_LEN);
    strncpy(ethCfg.ntp[1], "time.google.com", Host::MAX_FQDN_LEN);

    if (ethProfile.setConfig(ethCfg)) {
        Serial.println("ETH profile configured (static IP)");
    }

    // -------------------------------------------------------------------------
    // 3. Save profiles to Preferences
    // -------------------------------------------------------------------------
    if (wifiProfile.saveCfg(NS_WIFI)) {
        Serial.println("WiFi profile saved");
    }
    if (ethProfile.saveCfg(NS_ETH)) {
        Serial.println("ETH profile saved");
    }

    // -------------------------------------------------------------------------
    // 4. Load profiles back
    // -------------------------------------------------------------------------
    WiFiProfile wifiLoaded;
    if (wifiLoaded.loadCfg(NS_WIFI)) {
        char ssid[WiFiProfile::MAX_SSID_SIZE];
        wifiLoaded.getSsid(ssid, sizeof(ssid));
        Serial.printf("WiFi profile loaded — SSID: %s\n", ssid);
    }

    // -------------------------------------------------------------------------
    // 5. MAC address
    // -------------------------------------------------------------------------
    NetworkProfile::MACAddress mac;
    char macStr[NetworkProfile::MAC_STR_SIZE];

    // Default (hardware) MAC
    wifiProfile.getMac(mac, NetworkProfile::ConfigSource::FACTORY);
    NetworkProfile::macToStr(macStr, sizeof(macStr), mac);
    Serial.printf("WiFi default MAC: %s\n", macStr);

    // Set a custom MAC override
    NetworkProfile::MACAddress customMac = {0x4E, 0x52, 0x47, 0x00, 0x01, 0x02};
    if (wifiProfile.setMac(customMac)) {
        wifiProfile.getMac(mac);  // ACTIVE — returns override
        NetworkProfile::macToStr(macStr, sizeof(macStr), mac);
        Serial.printf("WiFi active MAC (override): %s\n", macStr);
    }

    // -------------------------------------------------------------------------
    // 6. Hostname — active or default (MAC-based)
    // -------------------------------------------------------------------------
    char hostname[NetworkProfile::MAX_HOSTNAME_SIZE];
    wifiProfile.getHostname(hostname, sizeof(hostname));
    Serial.printf("WiFi hostname (active): %s\n", hostname);

    wifiProfile.getHostname(hostname, sizeof(hostname),
                            NetworkProfile::ConfigSource::FACTORY);
    Serial.printf("WiFi hostname (default): %s\n", hostname);

    // -------------------------------------------------------------------------
    // 7. JSON serialisation
    // -------------------------------------------------------------------------
    char json[NetworkProfile::JSON_SIZE];
    if (wifiProfile.toJson(json, sizeof(json))) {
        Serial.printf("WiFi JSON:\n%s\n", json);
    }
    if (ethProfile.toJson(json, sizeof(json))) {
        Serial.printf("ETH JSON:\n%s\n", json);
    }

    // -------------------------------------------------------------------------
    // 8. Factory reset (clear saved data)
    // -------------------------------------------------------------------------
    // wifiProfile.clearCfg(NS_WIFI);  // Uncomment to clear WiFi profile
    // ethProfile.clearCfg(NS_ETH);    // Uncomment to clear ETH profile
}

void loop() {
    // The NetworkManager (separate library) would use these profiles
    // to apply configuration to the hardware interfaces:
    //
    //   networkManager.apply(ethProfile);   // Apply ETH config
    //   networkManager.apply(wifiProfile);  // Apply WiFi config (fallback)
}
