#ifndef BT_KEYBOARD_HOST_H
#define BT_KEYBOARD_HOST_H

#include <Arduino.h>
#include <vector>
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp32-hal-bt.h"
// HANYA alokasikan memori BLE (Classic BT dilepas untuk menghemat ~80 KB RAM)
#if __has_include("esp32-hal-alloc-ble-mem.h")
#include "esp32-hal-alloc-ble-mem.h"
#endif

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_hidh.h"
#if __has_include("esp_hidh_gattc.h")
#include "esp_hidh_gattc.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif
void esp_hidh_gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
#ifdef __cplusplus
}
#endif
#include "Config.h"
#include "StorageManager.h"

enum SpecialKeyEvent {
  KEY_NONE = 0,
  KEY_UP,
  KEY_DOWN,
  KEY_LEFT,
  KEY_RIGHT,
  KEY_ENTER,
  KEY_ESC,
  KEY_BACKSPACE,
  KEY_TAB,
  KEY_EXIT_SSH  // Dipicu oleh kombinasi Right-CTRL + C
};

struct KeyEvent {
  char ascii;
  SpecialKeyEvent special;
  bool isCtrl;
  bool isShift;
  bool isAlt;
};

typedef void (*KeyEventHandler)(const KeyEvent& event);

struct DiscoveredDevice {
  String name;
  String address;
  esp_bd_addr_t bda;
  uint8_t ble_addr_type;  // Public atau Random
  int rssi;
};

class BTKeyboardHost;
extern BTKeyboardHost BTKeyboard;

class BTKeyboardHost {
private:
  bool isInitialized = false;
  bool isConnected = false;
  String connectedDeviceName = "";
  String connectedDeviceAddress = "";

  std::vector<DiscoveredDevice> scanList;
  KeyEventHandler eventHandler = nullptr;
  uint8_t prevKeys[6] = {0, 0, 0, 0, 0, 0};

  volatile bool isScanningBLE = false;

public:
  void setEventHandler(KeyEventHandler handler) {
    eventHandler = handler;
  }

  bool getIsConnected() const {
    return isConnected;
  }

  String getConnectedName() const {
    return connectedDeviceName;
  }

  String getConnectedAddress() const {
    return connectedDeviceAddress;
  }

  bool getIsScanning() const {
    return isScanningBLE;
  }

  void clearScanResults() {
    scanList.clear();
  }

  void begin() {
    if (isInitialized) return;

    Serial.println("[BT] Menginisialisasi Bluetooth Low Energy (BLE Only) Keyboard Host...");

    // 0. Pastikan NVS Flash aktif
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase();
      nvs_flash_init();
    }

    // 1. LEPAS MEMORI BLUETOOTH CLASSIC KE HEAP (Menghemat ~50 KB - 80 KB RAM penting!)
    esp_err_t mem_rel = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (mem_rel == ESP_OK) {
      Serial.println("[BT] Memori Bluetooth Classic BERHASIL dilepas ke Heap (~50 KB RAM terbebas)!");
    }

    // 2. Inisialisasi Bluetooth Controller dalam mode BLE ONLY
    if (!btStarted()) {
      Serial.println("[BT] Memulai Bluetooth Controller (Mode: BLE Only)...");
      if (!btStartMode(BT_MODE_BLE)) {
        esp_bt_controller_status_t c_status = esp_bt_controller_get_status();
        if (c_status == ESP_BT_CONTROLLER_STATUS_IDLE) {
          esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
          bt_cfg.mode = ESP_BT_MODE_BLE;
          esp_bt_controller_init(&bt_cfg);
        }
        esp_bt_controller_enable(ESP_BT_MODE_BLE);
      }
    }

    esp_bt_controller_status_t final_c_status = esp_bt_controller_get_status();
    Serial.printf("[BT] Controller Status: %s (Mode: BLE Only, Kode: %d)\n", 
                  final_c_status == ESP_BT_CONTROLLER_STATUS_ENABLED ? "ENABLED (OK)" : "TIDAK AKTIF",
                  (int)final_c_status);

    if (final_c_status != ESP_BT_CONTROLLER_STATUS_ENABLED) {
      Serial.println("[BT] PERINGATAN: Bluetooth Controller gagal diaktifkan!");
      return;
    }

    // 3. Inisialisasi Host Stack Bluedroid
    esp_bluedroid_status_t b_status = esp_bluedroid_get_status();
    if (b_status == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
      esp_bluedroid_init();
    }

    b_status = esp_bluedroid_get_status();
    if (b_status == ESP_BLUEDROID_STATUS_INITIALIZED) {
      esp_bluedroid_enable();
    }

    Serial.println("[BT] Bluedroid Host Stack (BLE): ENABLED (OK)");

    // 4. Register Callbacks BLE GAP & GATTC
    Serial.println("[BT] Mendaftarkan Callbacks (BLE GAP & GATTC)...");
    esp_ble_gap_register_callback(bleGapCallbackStatic);
    esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler);

    // 5. Konfigurasi Security BLE
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t ble_iocap = ESP_IO_CAP_KBDISP;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &ble_iocap, sizeof(uint8_t));

    // 6. Inisialisasi Unified HID Host (BLE)
    esp_hidh_config_t hidh_cfg = {
      .callback = hidhCallbackStatic,
      .event_stack_size = 4096,
      .callback_arg = NULL
    };
    esp_err_t ret = esp_hidh_init(&hidh_cfg);
    if (ret != ESP_OK) {
      Serial.printf("[BT] Gagal inisialisasi esp_hidh: 0x%x\n", ret);
      return;
    }

    isInitialized = true;
    Serial.printf("[BT] Stack BLE Keyboard Host BERHASIL AKTIF! Free Heap: %u bytes\n", ESP.getFreeHeap());
  }

  // -------------------------------------------------------------
  // PEMINDAIAN (SCANNING BLE)
  // -------------------------------------------------------------
  void startScan(int durationSeconds = 6) {
    if (!isInitialized) begin();
    if (!isInitialized) {
      Serial.println("[BT-BLE] Tidak dapat memindai: Bluetooth belum berhasil aktif.");
      return;
    }
    scanList.clear();
    Serial.printf("[BT-BLE] Memindai Keyboard Bluetooth Low Energy (BLE) selama %d detik...\n", durationSeconds);
    Serial.println("[BT-BLE] Pastikan keyboard dalam Mode Pairing (lampu berkedip cepat)...");
    isScanningBLE = true;

    esp_ble_scan_params_t ble_scan_params = {
      .scan_type              = BLE_SCAN_TYPE_ACTIVE,
      .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
      .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
      .scan_interval          = 0x50,
      .scan_window            = 0x30,
      .scan_duplicate         = BLE_SCAN_DUPLICATE_ENABLE
    };
    esp_ble_gap_set_scan_params(&ble_scan_params);
    esp_err_t err = esp_ble_gap_start_scanning(durationSeconds);
    if (err != ESP_OK) {
      Serial.printf("[BT-BLE] Gagal memulai scanning: 0x%x\n", err);
      isScanningBLE = false;
    }
  }

  void printScanResults() {
    Serial.println("\n================ HASIL PEMINDAIAN BLE KEYBOARD ================");
    Serial.printf("%-4s | %-24s | %-18s | %-5s\n", "No", "Nama Perangkat", "MAC Address", "RSSI");
    Serial.println("---------------------------------------------------------------");

    for (size_t i = 0; i < scanList.size(); i++) {
      DiscoveredDevice& d = scanList[i];
      Serial.printf("[%2d] | %-24s | %-18s | %d dBm\n", 
                    (int)(i + 1),
                    d.name.c_str(), 
                    d.address.c_str(), 
                    d.rssi);
    }

    if (scanList.empty()) {
      Serial.println("[BT] Tidak ada perangkat ditemukan. Pastikan keyboard dalam mode pairing.");
    }
    Serial.println("===============================================================\n");
  }

  int getScanCount() const {
    return scanList.size();
  }

  DiscoveredDevice getDiscoveredDevice(int idx) const {
    if (idx >= 0 && idx < (int)scanList.size()) {
      return scanList[idx];
    }
    return DiscoveredDevice();
  }

  // -------------------------------------------------------------
  // KONEKSI PERANGKAT BLE
  // -------------------------------------------------------------
  bool connectByIndex(int index) {
    if (index < 1 || index > (int)scanList.size()) {
      Serial.println("[BT] Nomor index tidak valid!");
      return false;
    }
    DiscoveredDevice dev = scanList[index - 1];
    Serial.printf("[BT] Menghubungkan ke #%d: %s [%s] (BLE)...\n", 
                  index, dev.name.c_str(), dev.address.c_str());

    bool ok = connectRaw(dev.bda, dev.ble_addr_type);
    if (ok) {
      Storage.saveBT(dev.address, BT_TRANSPORT_BLE);
      connectedDeviceName = dev.name;
      connectedDeviceAddress = dev.address;
      Serial.println("[BT] MAC BLE Keyboard berhasil disimpan ke Flash NVS.");
    }
    return ok;
  }

  bool injectAndConnect(const String& macStr) {
    String cleanMac = macStr;
    cleanMac.trim();
    if (cleanMac.length() != 17) {
      Serial.println("[BT] Format MAC salah! Gunakan XX:XX:XX:XX:XX:XX (17 karakter)");
      return false;
    }

    esp_bd_addr_t bda;
    if (!parseMAC(cleanMac, bda)) {
      Serial.println("[BT] Parsing MAC Address gagal!");
      return false;
    }

    Serial.printf("[BT] Menginjeksi MAC BLE: %s...\n", cleanMac.c_str());

    bool ok = connectRaw(bda, BLE_ADDR_TYPE_PUBLIC);
    if (ok) {
      Storage.saveBT(cleanMac, BT_TRANSPORT_BLE);
      connectedDeviceName = "Bluetooth BLE Keyboard";
      connectedDeviceAddress = cleanMac;
      Serial.println("[BT] MAC berhasil diinjeksi dan disimpan ke Flash!");
    }
    return ok;
  }

  void tryConnectSaved() {
    if (Storage.config.btKeyboardMac.length() == 17 && !isConnected) {
      Serial.printf("[BT] Menghubungkan otomatis ke MAC tersimpan: %s [BLE]...\n", 
                    Storage.config.btKeyboardMac.c_str());
      esp_bd_addr_t bda;
      if (parseMAC(Storage.config.btKeyboardMac, bda)) {
        connectRaw(bda, BLE_ADDR_TYPE_PUBLIC);
      }
    }
  }

  bool connectRaw(esp_bd_addr_t bda, uint8_t ble_addr_type) {
    if (!isInitialized) begin();

    Serial.println("[BT] Membuka koneksi BLE HID Host...");

    esp_hidh_dev_t *dev = esp_hidh_dev_open(bda, ESP_HID_TRANSPORT_BLE, ble_addr_type);
    
    // Jika gagal di percobaan pertama, coba tipe address alternatif (Public <-> Random)
    if (dev == NULL) {
      uint8_t altType = (ble_addr_type == BLE_ADDR_TYPE_PUBLIC) ? BLE_ADDR_TYPE_RANDOM : BLE_ADDR_TYPE_PUBLIC;
      Serial.printf("[BT-BLE] Mencoba tipe alamat alternatif (%s)...\n", 
                    altType == BLE_ADDR_TYPE_RANDOM ? "RANDOM" : "PUBLIC");
      dev = esp_hidh_dev_open(bda, ESP_HID_TRANSPORT_BLE, altType);
    }

    if (dev == NULL) {
      Serial.println("[BT] esp_hidh_dev_open mengembalikan NULL (perangkat di luar jangkauan atau terhubung ke host lain).");
      return false;
    }

    Serial.println("[BT] Permintaan koneksi terkirim. Menunggu konfirmasi handshake...");
    return true;
  }

  void disconnect() {
    isConnected = false;
  }

  void handleReport(const uint8_t* pData, size_t length) {
    if (length < 3 || pData == nullptr) return;

    uint8_t modifier = pData[0];
    bool isLeftCtrl   = (modifier & 0x01) != 0;
    bool isLeftShift  = (modifier & 0x02) != 0;
    bool isLeftAlt    = (modifier & 0x04) != 0;
    bool isRightCtrl  = (modifier & 0x10) != 0;
    bool isRightShift = (modifier & 0x20) != 0;
    bool isRightAlt   = (modifier & 0x40) != 0;

    bool isShift = isLeftShift || isRightShift;
    bool isCtrl  = isLeftCtrl;
    bool isAlt   = isLeftAlt || isRightAlt;

    for (size_t i = 2; i < length && i < 8; i++) {
      uint8_t keycode = pData[i];
      if (keycode == 0) continue;

      bool alreadyPressed = false;
      for (int k = 0; k < 6; k++) {
        if (prevKeys[k] == keycode) {
          alreadyPressed = true;
          break;
        }
      }
      if (alreadyPressed) continue;

      // SHORTCUT RIGHT-CTRL + C: EXIT DARI SSH
      if (isRightCtrl && keycode == 0x06) {
        Serial.println("\n[BT KEY] >> SHORTCUT: RIGHT-CTRL + C TERDETEKSI! KELUAR DARI SESI SSH <<");
        if (eventHandler) {
          KeyEvent ev;
          ev.ascii = 0;
          ev.special = KEY_EXIT_SSH;
          ev.isCtrl = true;
          ev.isShift = false;
          ev.isAlt = false;
          eventHandler(ev);
        }
        continue;
      }

      KeyEvent ev = translateKeycode(keycode, isShift, isCtrl, isAlt);
      if (ev.ascii != 0 || ev.special != KEY_NONE) {
        if (eventHandler) {
          eventHandler(ev);
        }
      }
    }

    for (int k = 0; k < 6; k++) {
      prevKeys[k] = (k + 2 < (int)length) ? pData[k + 2] : 0;
    }
  }

private:
  static void hidhCallbackStatic(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    esp_hidh_event_t event = (esp_hidh_event_t)id;
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;

    switch (event) {
      case ESP_HIDH_OPEN_EVENT:
        if (param->open.status == ESP_OK) {
          const char* devName = esp_hidh_dev_name_get(param->open.dev);
          BTKeyboard.isConnected = true;
          BTKeyboard.connectedDeviceName = (devName != NULL && strlen(devName) > 0) ? devName : "Bluetooth Keyboard";
          Serial.printf("\n[BT] >> KEYBOARD BERHASIL TERHUBUNG: %s <<\n", BTKeyboard.connectedDeviceName.c_str());
        } else {
          Serial.printf("[BT] Gagal konek, status: %d\n", param->open.status);
          BTKeyboard.isConnected = false;
        }
        break;

      case ESP_HIDH_INPUT_EVENT:
        if (param->input.length > 0 && param->input.data != nullptr) {
          BTKeyboard.handleReport(param->input.data, param->input.length);
        }
        break;

      case ESP_HIDH_CLOSE_EVENT:
        Serial.println("\n[BT] Keyboard Bluetooth terputus (Disconnected).");
        BTKeyboard.isConnected = false;
        break;

      default:
        break;
    }
  }

  static void bleGapCallbackStatic(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
      case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        if (scan_result->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
          DiscoveredDevice dev;
          char bdaStr[18];
          snprintf(bdaStr, sizeof(bdaStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                   scan_result->scan_rst.bda[0], scan_result->scan_rst.bda[1], scan_result->scan_rst.bda[2],
                   scan_result->scan_rst.bda[3], scan_result->scan_rst.bda[4], scan_result->scan_rst.bda[5]);
          dev.address = bdaStr;
          memcpy(dev.bda, scan_result->scan_rst.bda, 6);
          dev.ble_addr_type = scan_result->scan_rst.ble_addr_type;
          dev.rssi = scan_result->scan_rst.rssi;
          dev.name = "(BLE Device)";

          uint8_t adv_name_len = 0;
          uint8_t *adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
          if (adv_name == NULL) {
            adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_SHORT, &adv_name_len);
          }
          if (adv_name != NULL && adv_name_len > 0) {
            char nameBuf[64];
            if (adv_name_len >= 64) adv_name_len = 63;
            memcpy(nameBuf, adv_name, adv_name_len);
            nameBuf[adv_name_len] = '\0';
            dev.name = nameBuf;
          }

          bool exists = false;
          for (auto& item : BTKeyboard.scanList) {
            if (item.address.equalsIgnoreCase(dev.address)) {
              exists = true;
              if (dev.name != "(BLE Device)") item.name = dev.name;
              break;
            }
          }
          if (!exists) {
            BTKeyboard.scanList.push_back(dev);
            Serial.printf("[Scan-BLE] Menemukan: %-20s | %s | %d dBm\n", dev.name.c_str(), dev.address.c_str(), dev.rssi);
          }
        } else if (scan_result->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
          BTKeyboard.isScanningBLE = false;
          Serial.println("[BT-BLE] Pemindaian BLE selesai.");
          BTKeyboard.printScanResults();
        }
        break;
      }

      case ESP_GAP_BLE_SEC_REQ_EVT:
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;

      default:
        break;
    }
  }

  bool parseMAC(const String& str, esp_bd_addr_t bda) {
    unsigned int bytes[6];
    if (sscanf(str.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
               &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) == 6 ||
        sscanf(str.c_str(), "%02X:%02X:%02X:%02X:%02X:%02X",
               &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) == 6) {
      for (int i = 0; i < 6; i++) bda[i] = (uint8_t)bytes[i];
      return true;
    }
    return false;
  }

  KeyEvent translateKeycode(uint8_t code, bool shift, bool ctrl, bool alt) {
    KeyEvent ev;
    ev.ascii = 0;
    ev.special = KEY_NONE;
    ev.isCtrl = ctrl;
    ev.isShift = shift;
    ev.isAlt = alt;

    if (code == 0x52) { ev.special = KEY_UP; return ev; }
    if (code == 0x51) { ev.special = KEY_DOWN; return ev; }
    if (code == 0x50) { ev.special = KEY_LEFT; return ev; }
    if (code == 0x4F) { ev.special = KEY_RIGHT; return ev; }
    if (code == 0x28) { ev.special = KEY_ENTER; ev.ascii = '\r'; return ev; }
    if (code == 0x29) { ev.special = KEY_ESC; ev.ascii = 27; return ev; }
    if (code == 0x2A) { ev.special = KEY_BACKSPACE; ev.ascii = 8; return ev; }
    if (code == 0x2B) { ev.special = KEY_TAB; ev.ascii = '\t'; return ev; }
    if (code == 0x2C) { ev.ascii = ' '; return ev; }

    if (code >= 0x04 && code <= 0x1D) {
      char base = 'a' + (code - 0x04);
      if (ctrl) {
        ev.ascii = (base - 'a') + 1;
      } else if (shift) {
        ev.ascii = base - 32;
      } else {
        ev.ascii = base;
      }
      return ev;
    }

    if (code >= 0x1E && code <= 0x26) {
      int num = code - 0x1E;
      const char numNormal[] = "123456789";
      const char numShift[]  = "!@#$%^&*(";
      ev.ascii = shift ? numShift[num] : numNormal[num];
      return ev;
    }
    if (code == 0x27) {
      ev.ascii = shift ? ')' : '0';
      return ev;
    }

    switch (code) {
      case 0x2D: ev.ascii = shift ? '_' : '-'; break;
      case 0x2E: ev.ascii = shift ? '+' : '='; break;
      case 0x2F: ev.ascii = shift ? '{' : '['; break;
      case 0x30: ev.ascii = shift ? '}' : ']'; break;
      case 0x31: ev.ascii = shift ? '|' : '\\'; break;
      case 0x33: ev.ascii = shift ? ':' : ';'; break;
      case 0x34: ev.ascii = shift ? '"' : '\''; break;
      case 0x35: ev.ascii = shift ? '~' : '`'; break;
      case 0x36: ev.ascii = shift ? '<' : ','; break;
      case 0x37: ev.ascii = shift ? '>' : '.'; break;
      case 0x38: ev.ascii = shift ? '?' : '/'; break;
      default: break;
    }

    return ev;
  }
};

extern BTKeyboardHost BTKeyboard;

#endif // BT_KEYBOARD_HOST_H
