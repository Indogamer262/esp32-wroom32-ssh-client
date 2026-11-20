/**
 * ============================================================================
 * Proyek: ESP32 Wroom-32 SSH Client dengan LCD IPS 240x240 ST7789 & BT Keyboard
 * ============================================================================
 * 
 * Konfigurasi Pin Hardware:
 * • VCC        -> 3.3V
 * • GND        -> GND
 * • SCL (SCLK) -> GPIO 18 (VSPI CLK)
 * • SDA (MOSI) -> GPIO 23 (VSPI MOSI)
 * • RES / RST  -> GPIO 4
 * • DC         -> GPIO 2
 * • CS         -> TIDAK ADA (Hardwired ke GND pada modul 7-pin, set -1)
 * • BLK / BL   -> GPIO 32 (PWM Backlight)
 *                 PERINGATAN: GPIO 36 adalah pin INPUT ONLY dan TIDAK BISA
 *                 menghasilkan sinyal PWM/output. Sangat disarankan gunakan
 *                 GPIO 32 atau sambungkan pin BLK langsung ke 3.3V!
 *
 * Fitur:
 * 1. Bluetooth Keyboard Host (BLE HID) menggunakan NimBLE-Arduino.
 * 2. Serial Interactive Menu untuk pindai perangkat Bluetooth (nama & MAC),
 *    pilih hasil scan, atau injeksi MAC manual.
 * 3. Simpan konfigurasi (WiFi, SSH Server IP/Port/User, BT MAC) ke Flash NVS.
 * 4. UI ST7789 240x240 IPS: Main Menu, Form WiFi, Form Target SSH Server,
 *    dan Terminal Grid VT100 Emulator 40x28 karakter.
 * 5. Navigasi Arrow Keys, ESC untuk kembali dari menu.
 * 6. Shortcut Khusus Right-CTRL + C untuk keluar dari sesi SSH aktif ke Menu.
 * 7. LibSSH-ESP32 client untuk koneksi SSH interaktif remote shell (PTY).
 * ============================================================================
 */

#include <Arduino.h>
#include "Config.h"
#include "StorageManager.h"
#include "BTKeyboardHost.h"
#include "DisplayUI.h"
#include "SSHClientManager.h"

// Definisi Instance Global
StorageManager Storage;
BTKeyboardHost BTKeyboard;
DisplayUI UI;
SSHClientManager SSHClient;

// Buffer input perintah dari Serial
String serialInputBuffer = "";

// Forward Declarations
void printSerialHelpMenu();
void processSerialCommand(const String& cmd);
void onKeyEvent(const KeyEvent& ev);

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  delay(1000);

  Serial.println("\n\n========================================================");
  Serial.println("   ESP32 WROOM-32 SSH CLIENT & BLUETOOTH KEYBOARD HOST   ");
  Serial.println("========================================================");

  // 1. Inisialisasi Flash Storage NVS
  Serial.println("[INIT] Menginisialisasi Flash Storage (Preferences/NVS)...");
  Storage.begin();
  Storage.printCurrentConfig();

  // 2. Inisialisasi Layar LCD ST7789 IPS 240x240
  Serial.println("[INIT] Menginisialisasi Layar ST7789 240x240 IPS...");
  UI.begin();

  // 3. Inisialisasi Bluetooth Keyboard Host (NimBLE)
  Serial.println("[INIT] Menginisialisasi Bluetooth BLE Keyboard Host...");
  BTKeyboard.begin();
  BTKeyboard.setEventHandler(onKeyEvent);

  // 4. Inisialisasi SSH Client Engine
  Serial.println("[INIT] Menginisialisasi Engine SSH Client...");
  SSHClient.begin();

  // 5. Coba hubungkan otomatis ke Bluetooth Keyboard yang tersimpan
  BTKeyboard.tryConnectSaved();

  // 6. Tampilkan Menu Serial
  printSerialHelpMenu();
}

void loop() {
  // 1. Pembacaan dan eksekusi Serial Menu CLI
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (serialInputBuffer.length() > 0) {
        processSerialCommand(serialInputBuffer);
        serialInputBuffer = "";
      }
    } else {
      serialInputBuffer += c;
    }
  }

  // 2. Jika sedang berada dalam Sesi Terminal SSH
  if (UI.getState() == STATE_SSH_TERMINAL) {
    if (!SSHClient.getIsConnected()) {
      // Buka sesi SSH jika belum terkoneksi
      SSHClient.startSession();
    } else {
      // Loop non-blocking reader untuk SSH stream
      SSHClient.loop();
    }
  }

  delay(5);
}

// ============================================================================
// HANDLER EVENT KEYBOARD BLUETOOTH
// ============================================================================
void onKeyEvent(const KeyEvent& ev) {
  // -------------------------------------------------------------
  // SHORTCUT KHUSUS: RIGHT-CTRL + C ==> KELUAR DARI SESI SSH
  // -------------------------------------------------------------
  if (ev.special == KEY_EXIT_SSH) {
    if (UI.getState() == STATE_SSH_TERMINAL) {
      Serial.println("\n[SISTEM] Right-CTRL + C diterima! Menutup sesi SSH dan kembali ke Menu...");
      SSHClient.closeSession();
      UI.setState(STATE_MAIN_MENU);
    }
    return;
  }

  // Jika sedang di dalam Terminal SSH yang aktif
  if (UI.getState() == STATE_SSH_TERMINAL && SSHClient.getIsConnected()) {
    if (ev.special == KEY_UP) {
      const char upSeq[] = "\033[A";
      SSHClient.sendInput(upSeq, 3);
    } else if (ev.special == KEY_DOWN) {
      const char downSeq[] = "\033[B";
      SSHClient.sendInput(downSeq, 3);
    } else if (ev.special == KEY_RIGHT) {
      const char rightSeq[] = "\033[C";
      SSHClient.sendInput(rightSeq, 3);
    } else if (ev.special == KEY_LEFT) {
      const char leftSeq[] = "\033[D";
      SSHClient.sendInput(leftSeq, 3);
    } else if (ev.special == KEY_ENTER) {
      char cr = '\r';
      SSHClient.sendInput(&cr, 1);
    } else if (ev.special == KEY_BACKSPACE) {
      char bs = 0x7F; // DEL / Backspace untuk terminal Linux
      SSHClient.sendInput(&bs, 1);
    } else if (ev.special == KEY_TAB) {
      char tab = '\t';
      SSHClient.sendInput(&tab, 1);
    } else if (ev.special == KEY_ESC) {
      char esc = 27;
      SSHClient.sendInput(&esc, 1);
    } else if (ev.ascii != 0) {
      SSHClient.sendInput(&ev.ascii, 1);
    }
    return;
  }

  // Jika sedang di Menu LCD atau Form Pengaturan
  UI.handleKeyInput(ev);
}

// ============================================================================
// MENU & PERINTAH SERIAL MONITOR (115200 BAUD)
// ============================================================================
void printSerialHelpMenu() {
  Serial.println("\n========================================================");
  Serial.println("     MENU SERIAL UTAMA (BLUETOOTH DUAL-MODE & SSH)      ");
  Serial.println("========================================================");
  Serial.println("1. scan            : Pindai TERPADU (Classic BT + BLE sekaligus)");
  Serial.println("   scan classic    : Pindai khusus Bluetooth Classic (BR/EDR)");
  Serial.println("   scan ble        : Pindai khusus Bluetooth Low Energy (BLE)");
  Serial.println("2. select <no>     : Hubungkan ke perangkat hasil scan (Contoh: select 1)");
  Serial.println("3. inject <MAC>    : Injeksi MAC Address manual (Contoh: inject 11:21:AC:00:03:58)");
  Serial.println("   inject classic <MAC> / inject ble <MAC>");
  Serial.println("4. wifi <ssid> <pw>: Atur SSID & Password WiFi via Serial");
  Serial.println("5. ssh <ip> <port> <user> <pass> : Atur target SSH Server");
  Serial.println("6. status          : Tampilkan status koneksi & NVS Flash");
  Serial.println("7. reboot          : Restart ESP32");
  Serial.println("8. help            : Tampilkan menu ini lagi");
  Serial.println("--------------------------------------------------------");
  Serial.println("Ketik nomor pilihan (1-8) atau teks perintah di atas:");
  Serial.println("========================================================\n");
}

void processSerialCommand(const String& cmd) {
  String input = cmd;
  input.trim();
  if (input.length() == 0) return;

  Serial.printf("\n[SERIAL COMMAND]: %s\n", input.c_str());

  if (input.equalsIgnoreCase("scan classic")) {
    BTKeyboard.startClassicScan(6);
    delay(6500);
    BTKeyboard.printScanResults();
    Serial.println("[SERIAL] Untuk menghubungkan ke perangkat, ketik: select <nomor>");
  }
  else if (input.equalsIgnoreCase("scan ble")) {
    BTKeyboard.startBLEScan(6);
    delay(6500);
    BTKeyboard.printScanResults();
    Serial.println("[SERIAL] Untuk menghubungkan ke perangkat, ketik: select <nomor>");
  }
  else if (input.equalsIgnoreCase("1") || input.equalsIgnoreCase("scan") || input.equalsIgnoreCase("scan dual")) {
    BTKeyboard.startDualScan(5);
    Serial.println("[SERIAL] Untuk menghubungkan ke perangkat, ketik: select <nomor>");
  }
  else if (input.startsWith("select ")) {
    int idx = input.substring(7).toInt();
    BTKeyboard.connectByIndex(idx);
    UI.drawMainMenu();
  }
  else if (input.startsWith("inject")) {
    // Format: inject [classic|ble] XX:XX:XX:XX:XX:XX
    String args = input.substring(6);
    args.trim();

    uint8_t transport = BT_TRANSPORT_CLASSIC; // Default ke Classic untuk keyboard pengguna
    String mac = args;

    if (args.startsWith("classic ") || args.startsWith("Classic ")) {
      transport = BT_TRANSPORT_CLASSIC;
      mac = args.substring(8);
      mac.trim();
    } else if (args.startsWith("ble ") || args.startsWith("BLE ")) {
      transport = BT_TRANSPORT_BLE;
      mac = args.substring(4);
      mac.trim();
    }

    if (mac.length() == 17) {
      BTKeyboard.injectAndConnect(mac, transport);
      UI.drawMainMenu();
    } else {
      Serial.println("[SERIAL] Format salah! Contoh:");
      Serial.println("  inject 11:21:AC:00:03:58          (Default: Classic BT)");
      Serial.println("  inject classic 11:21:AC:00:03:58  (Classic BR/EDR)");
      Serial.println("  inject ble F4:73:35:12:34:56      (Bluetooth Low Energy)");
    }
  }
  else if (input.equalsIgnoreCase("3") || input.equalsIgnoreCase("status") || input.equalsIgnoreCase("6")) {
    Storage.printCurrentConfig();
    Serial.printf("WiFi Status    : %s (IP: %s)\n", 
                  WiFi.status() == WL_CONNECTED ? "TERKONEKSI" : "TERPUTUS",
                  WiFi.localIP().toString().c_str());
    Serial.printf("BT Keyboard    : %s (%s) [Mode: %s]\n", 
                  BTKeyboard.getIsConnected() ? "TERHUBUNG" : "TERPUTUS",
                  BTKeyboard.getConnectedAddress().c_str(),
                  BTKeyboard.getConnectedTransport() == BT_TRANSPORT_CLASSIC ? "Classic BR/EDR" : "BLE");
    Serial.printf("Free Heap RAM  : %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Min Free Heap  : %u bytes\n", ESP.getMinFreeHeap());
  }
  else if (input.startsWith("wifi ")) {
    int firstSpace = input.indexOf(' ');
    int secondSpace = input.indexOf(' ', firstSpace + 1);
    String ssid = "";
    String pass = "";
    if (secondSpace > 0) {
      ssid = input.substring(firstSpace + 1, secondSpace);
      pass = input.substring(secondSpace + 1);
    } else {
      ssid = input.substring(firstSpace + 1);
    }
    Storage.saveWiFi(ssid, pass);
    Serial.printf("[SERIAL] WiFi berhasil disimpan: SSID='%s'\n", ssid.c_str());
    UI.drawMainMenu();
  }
  else if (input.startsWith("ssh ")) {
    // Format: ssh <ip> <port> <user> <pass>
    int s1 = input.indexOf(' ');
    int s2 = input.indexOf(' ', s1 + 1);
    int s3 = input.indexOf(' ', s2 + 1);
    int s4 = input.indexOf(' ', s3 + 1);

    if (s1 > 0 && s2 > 0 && s3 > 0) {
      String host = input.substring(s1 + 1, s2);
      uint16_t port = input.substring(s2 + 1, s3).toInt();
      String user = (s4 > 0) ? input.substring(s3 + 1, s4) : input.substring(s3 + 1);
      String pass = (s4 > 0) ? input.substring(s4 + 1) : "";

      Storage.saveSSH(host, port, user, pass);
      Serial.println("[SERIAL] Pengaturan SSH berhasil diperbarui & disimpan ke NVS!");
      UI.drawMainMenu();
    } else {
      Serial.println("[SERIAL] Format salah! Gunakan: ssh <host> <port> <user> <pass>");
    }
  }
  else if (input.equalsIgnoreCase("7") || input.equalsIgnoreCase("reboot")) {
    Serial.println("[SERIAL] Merestart ESP32...");
    delay(500);
    ESP.restart();
  }
  else if (input.equalsIgnoreCase("8") || input.equalsIgnoreCase("help") || input.equalsIgnoreCase("?")) {
    printSerialHelpMenu();
  }
  else {
    // Jika sedang dalam sesi SSH terminal, teruskan input Serial ke SSH channel juga
    if (UI.getState() == STATE_SSH_TERMINAL && SSHClient.getIsConnected()) {
      input += "\r";
      SSHClient.sendInput(input.c_str(), input.length());
    } else {
      Serial.println("[SERIAL] Perintah tidak dikenali. Ketik 'help' untuk daftar menu.");
    }
  }
}
