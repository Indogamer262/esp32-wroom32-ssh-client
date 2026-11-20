#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"

// Transport type: 0 = Bluetooth Classic (BR/EDR), 1 = Bluetooth Low Energy (BLE)
#define BT_TRANSPORT_CLASSIC 0
#define BT_TRANSPORT_BLE     1

struct SystemConfig {
  String wifiSSID;
  String wifiPass;
  String sshHost;
  uint16_t sshPort;
  String sshUser;
  String sshPass;
  String btKeyboardMac;
  uint8_t btTransportType; // 0 = Classic BT, 1 = BLE
  uint8_t screenBrightness;
};

class StorageManager {
private:
  Preferences prefs;
  const char* PREFS_NAMESPACE = "ssh_client";

public:
  SystemConfig config;

  void begin() {
    prefs.begin(PREFS_NAMESPACE, false);
    load();
  }

  void load() {
    config.wifiSSID = prefs.getString("wifi_ssid", "");
    config.wifiPass = prefs.getString("wifi_pass", "");
    config.sshHost  = prefs.getString("ssh_host", DEFAULT_SSH_HOST);
    config.sshPort  = prefs.getUShort("ssh_port", DEFAULT_SSH_PORT);
    config.sshUser  = prefs.getString("ssh_user", DEFAULT_SSH_USER);
    config.sshPass  = prefs.getString("ssh_pass", "");
    config.btKeyboardMac = prefs.getString("bt_mac", "");
    config.btTransportType = prefs.getUChar("bt_type", BT_TRANSPORT_CLASSIC);
    config.screenBrightness = prefs.getUChar("brightness", 255);
  }

  void saveWiFi(const String& ssid, const String& pass) {
    config.wifiSSID = ssid;
    config.wifiPass = pass;
    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_pass", pass);
  }

  void saveSSH(const String& host, uint16_t port, const String& user, const String& pass) {
    config.sshHost = host;
    config.sshPort = port;
    config.sshUser = user;
    config.sshPass = pass;
    prefs.putString("ssh_host", host);
    prefs.putUShort("ssh_port", port);
    prefs.putString("ssh_user", user);
    prefs.putString("ssh_pass", pass);
  }

  void saveBT(const String& mac, uint8_t transportType = BT_TRANSPORT_CLASSIC) {
    config.btKeyboardMac = mac;
    config.btTransportType = transportType;
    prefs.putString("bt_mac", mac);
    prefs.putUChar("bt_type", transportType);
  }

  void saveBrightness(uint8_t brightness) {
    config.screenBrightness = brightness;
    prefs.putUChar("brightness", brightness);
  }

  void printCurrentConfig() {
    Serial.println("\n--- [KONFIGURASI TERSIMPAN DI FLASH NVS] ---");
    Serial.printf("WiFi SSID     : %s\n", config.wifiSSID.c_str());
    Serial.printf("WiFi Password : %s\n", config.wifiPass.length() > 0 ? "********" : "(Belum diatur)");
    Serial.printf("SSH Target    : %s:%d\n", config.sshHost.c_str(), config.sshPort);
    Serial.printf("SSH User      : %s\n", config.sshUser.c_str());
    Serial.printf("SSH Password  : %s\n", config.sshPass.length() > 0 ? "********" : "(Kosong)");
    Serial.printf("BT Keyboard   : %s [Mode: %s]\n", 
                  config.btKeyboardMac.length() > 0 ? config.btKeyboardMac.c_str() : "(Belum dipasangkan)",
                  config.btTransportType == BT_TRANSPORT_CLASSIC ? "Classic BR/EDR" : "BLE");
    Serial.printf("Kecerahan LCD : %d / 255\n", config.screenBrightness);
    Serial.println("--------------------------------------------");
  }
};

extern StorageManager Storage;

#endif // STORAGE_MANAGER_H
