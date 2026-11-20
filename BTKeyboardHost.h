#ifndef BT_KEYBOARD_HOST_H
#define BT_KEYBOARD_HOST_H

#include <Arduino.h>
#include <vector>
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_gap_ble_api.h"
#include "esp_hidh.h"
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
  uint8_t transport;      // 0 = Classic (ESP_HID_TRANSPORT_BT), 1 = BLE (ESP_HID_TRANSPORT_BLE)
  uint8_t ble_addr_type;  // Public atau Random (khusus BLE)
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
  uint8_t connectedTransport = BT_TRANSPORT_CLASSIC;

  std::vector<DiscoveredDevice> scanList;
  KeyEventHandler eventHandler = nullptr;
  uint8_t prevKeys[6] = {0, 0, 0, 0, 0, 0};

  // State scanning
  volatile bool isScanningClassic = false;
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

  uint8_t getConnectedTransport() const {
    return connectedTransport;
  }

  void begin() {
    if (isInitialized) return;

    Serial.println("[BT] Menginisialisasi Native Bluetooth Dual-Mode (Classic + BLE)...");

    // Inisialisasi Bluetooth Controller dalam mode DUAL (BTDM)
    if (!btStarted()) {
      esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
      esp_err_t ret = esp_bt_controller_init(&bt_cfg);
      if (ret != ESP_OK) {
        Serial.printf("[BT] Gagal init controller: 0x%x\n", ret);
        return;
      }
      ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
      if (ret != ESP_OK) {
        Serial.printf("[BT] Gagal enable BTDM: 0x%x\n", ret);
        return;
      }
    }

    // Inisialisasi Stack Bluedroid
    esp_err_t ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
      Serial.printf("[BT] Gagal init Bluedroid: 0x%x\n", ret);
      return;
    }
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
      Serial.printf("[BT] Gagal enable Bluedroid: 0x%x\n", ret);
      return;
    }

    // Register Callbacks untuk Classic BT GAP & BLE GAP
    esp_bt_gap_register_callback(btGapCallbackStatic);
    esp_ble_gap_register_callback(bleGapCallbackStatic);

    // Konfigurasi Security & Discovery Mode Classic BT
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO; // Bisa input/output PIN
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    // Konfigurasi Security BLE
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t ble_iocap = ESP_BLE_IO_CAP_KBDISP;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &ble_iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

    // Inisialisasi Unified HID Host (Mendukung Classic BT & BLE sekaligus)
    esp_hidh_config_t hidh_cfg = {
      .callback = hidhCallbackStatic,
      .event_stack_size = 4096,
      .callback_arg = NULL
    };
    ret = esp_hidh_init(&hidh_cfg);
    if (ret != ESP_OK) {
      Serial.printf("[BT] Gagal inisialisasi esp_hidh: 0x%x\n", ret);
      return;
    }

    isInitialized = true;
    Serial.println("[BT] Stack Bluetooth Dual-Mode (Classic + BLE HID Host) SIAP!");
  }

  // -------------------------------------------------------------
  // PEMINDAIAN (SCANNING)
  // -------------------------------------------------------------
  // 1. Dual Scan: Memindai Bluetooth Classic lalu dilanjutkan BLE
  void startDualScan(int durationPerMode = 6) {
    scanList.clear();
    Serial.println("\n============================================================");
    Serial.println("  MEMULAI PEMINDAIAN TERPADU: BLUETOOTH CLASSIC + BLE SCAN  ");
    Serial.println("============================================================");
    Serial.println("[BT] Pastikan keyboard dalam Mode Pairing (lampu berkedip cepat)...");

    // Fase 1: Scan Classic Bluetooth (BR/EDR)
    startClassicScan(durationPerMode);
    while (isScanningClassic) {
      delay(100);
    }

    // Fase 2: Scan Bluetooth Low Energy (BLE)
    startBLEScan(durationPerMode);
    while (isScanningBLE) {
      delay(100);
    }

    printScanResults();
  }

  // 2. Scan Khusus Bluetooth Classic
  void startClassicScan(int durationSeconds = 6) {
    if (!isInitialized) begin();
    Serial.printf("[BT-Classic] Memindai perangkat Bluetooth Classic selama %d detik...\n", durationSeconds);
    isScanningClassic = true;
    uint8_t inq_len = (durationSeconds * 100) / 128;
    if (inq_len < 1) inq_len = 1;
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, inq_len, 0);
  }

  // 3. Scan Khusus BLE
  void startBLEScan(int durationSeconds = 6) {
    if (!isInitialized) begin();
    Serial.printf("[BT-BLE] Memindai perangkat Bluetooth Low Energy (BLE) selama %d detik...\n", durationSeconds);
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
    esp_ble_gap_start_scanning(durationSeconds);
  }

  void printScanResults() {
    Serial.println("\n================ HASIL PEMINDAIAN BLUETOOTH ================");
    Serial.printf("%-4s | %-9s | %-20s | %-18s | %-5s\n", "No", "Tipe", "Nama Perangkat", "MAC Address", "RSSI");
    Serial.println("------------------------------------------------------------");

    for (size_t i = 0; i < scanList.size(); i++) {
      DiscoveredDevice& d = scanList[i];
      Serial.printf("[%2d] | %-9s | %-20s | %-18s | %d dBm\n", 
                    i + 1,
                    d.transport == BT_TRANSPORT_CLASSIC ? "[Classic]" : "[BLE]",
                    d.name.c_str(), 
                    d.address.c_str(), 
                    d.rssi);
    }

    if (scanList.empty()) {
      Serial.println("[BT] Tidak ada perangkat ditemukan. Pastikan keyboard dalam mode pairing.");
    }
    Serial.println("============================================================\n");
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
  // KONEKSI PERANGKAT
  // -------------------------------------------------------------
  bool connectByIndex(int index) {
    if (index < 1 || index > (int)scanList.size()) {
      Serial.println("[BT] Nomor index tidak valid!");
      return false;
    }
    DiscoveredDevice dev = scanList[index - 1];
    Serial.printf("[BT] Menghubungkan ke #%d: %s [%s] (%s)...\n", 
                  index, dev.name.c_str(), dev.address.c_str(),
                  dev.transport == BT_TRANSPORT_CLASSIC ? "Classic" : "BLE");

    bool ok = connectRaw(dev.bda, dev.transport, dev.ble_addr_type);
    if (ok) {
      Storage.saveBT(dev.address, dev.transport);
      connectedDeviceName = dev.name;
      connectedDeviceAddress = dev.address;
      connectedTransport = dev.transport;
      Serial.println("[BT] MAC & Tipe Transport berhasil disimpan ke Flash internal.");
    }
    return ok;
  }

  // Injeksi MAC Manual (Bisa Classic atau BLE)
  bool injectAndConnect(const String& macStr, uint8_t transport = BT_TRANSPORT_CLASSIC) {
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

    Serial.printf("[BT] Menginjeksi MAC: %s [Mode: %s]...\n", 
                  cleanMac.c_str(), transport == BT_TRANSPORT_CLASSIC ? "Classic BT" : "BLE");

    bool ok = connectRaw(bda, transport, 0);
    if (ok) {
      Storage.saveBT(cleanMac, transport);
      connectedDeviceName = "Bluetooth Keyboard";
      connectedDeviceAddress = cleanMac;
      connectedTransport = transport;
      Serial.println("[BT] MAC berhasil diinjeksi dan disimpan ke Flash!");
    }
    return ok;
  }

  void tryConnectSaved() {
    if (Storage.config.btKeyboardMac.length() == 17 && !isConnected) {
      Serial.printf("[BT] Menghubungkan otomatis ke MAC tersimpan: %s [%s]...\n", 
                    Storage.config.btKeyboardMac.c_str(),
                    Storage.config.btTransportType == BT_TRANSPORT_CLASSIC ? "Classic BT" : "BLE");
      esp_bd_addr_t bda;
      if (parseMAC(Storage.config.btKeyboardMac, bda)) {
        connectRaw(bda, Storage.config.btTransportType, 0);
      }
    }
  }

  bool connectRaw(esp_bd_addr_t bda, uint8_t transport, uint8_t ble_addr_type) {
    if (!isInitialized) begin();

    Serial.printf("[BT] Membuka koneksi HID Host (Transport: %s)...\n", 
                  transport == BT_TRANSPORT_CLASSIC ? "ESP_HID_TRANSPORT_BT (Classic)" : "ESP_HID_TRANSPORT_BLE (BLE)");

    esp_hidh_dev_t *dev = esp_hidh_dev_open(bda, (esp_hid_transport_t)transport, ble_addr_type);
    if (dev == NULL) {
      Serial.println("[BT] esp_hidh_dev_open mengembalikan NULL (mungkin sudah terhubung atau target sibuk).");
      return false;
    }

    Serial.println("[BT] Permintaan koneksi terkirim. Menunggu konfirmasi handshake...");
    return true;
  }

  void disconnect() {
    isConnected = false;
  }

  // -------------------------------------------------------------
  // PARSING DATA REPORT KEYBOARD
  // -------------------------------------------------------------
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

  // Classic BT GAP Callback
  static void btGapCallbackStatic(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
      case ESP_BT_GAP_DISC_RES_EVT: {
        DiscoveredDevice dev;
        char bdaStr[18];
        snprintf(bdaStr, sizeof(bdaStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 param->disc_res.bda[0], param->disc_res.bda[1], param->disc_res.bda[2],
                 param->disc_res.bda[3], param->disc_res.bda[4], param->disc_res.bda[5]);
        dev.address = bdaStr;
        memcpy(dev.bda, param->disc_res.bda, 6);
        dev.transport = BT_TRANSPORT_CLASSIC;
        dev.ble_addr_type = 0;
        dev.rssi = -60;
        dev.name = "(Bluetooth Device)";

        for (int i = 0; i < param->disc_res.num_prop; i++) {
          if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_BDNAME) {
            char nameBuf[64];
            int len = param->disc_res.prop[i].len;
            if (len >= 64) len = 63;
            memcpy(nameBuf, param->disc_res.prop[i].val, len);
            nameBuf[len] = '\0';
            dev.name = nameBuf;
          } else if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_RSSI) {
            dev.rssi = *(int8_t*)param->disc_res.prop[i].val;
          }
        }

        // Cek duplikasi
        bool exists = false;
        for (auto& item : BTKeyboard.scanList) {
          if (item.address.equalsIgnoreCase(dev.address)) {
            exists = true;
            if (dev.name != "(Bluetooth Device)") item.name = dev.name;
            break;
          }
        }
        if (!exists) {
          BTKeyboard.scanList.push_back(dev);
          Serial.printf("[Scan-Classic] Menemukan: %-20s | %s | %d dBm\n", dev.name.c_str(), dev.address.c_str(), dev.rssi);
        }
        break;
      }

      case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
          BTKeyboard.isScanningClassic = false;
          Serial.println("[BT-Classic] Pemindaian Classic selesai.");
        }
        break;

      // Handling Pairing Classic BT (PIN / Passkey)
      case ESP_BT_GAP_KEY_NOTIF_EVT:
        Serial.println("\n============================================================");
        Serial.printf(">> PAIRING PASSKEY: KETIK [%06u] LALU TEKAN ENTER DI KEYBOARD <<\n", param->key_notif.passkey);
        Serial.println("============================================================\n");
        break;

      case ESP_BT_GAP_PIN_REQ_EVT: {
        esp_bt_pin_code_t pin_code;
        pin_code[0] = '0'; pin_code[1] = '0'; pin_code[2] = '0'; pin_code[3] = '0';
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        Serial.println("[BT] Auto-reply PIN: 0000");
        break;
      }

      case ESP_BT_GAP_CFM_REQ_EVT:
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;

      default:
        break;
    }
  }

  // BLE GAP Callback
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
          dev.transport = BT_TRANSPORT_BLE;
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
