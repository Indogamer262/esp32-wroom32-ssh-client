#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <WiFi.h>
#include <esp_arduino_version.h>
#include "Config.h"
#include "StorageManager.h"
#include "BTKeyboardHost.h"

// Warna Tema Modern (16-bit 565 format)
#define COLOR_BG          0x0842  // Dark Navy/Slate
#define COLOR_SURFACE     0x18E5  // Surface Panel
#define COLOR_PRIMARY     0x2D7F  // Modern Blue / Accent
#define COLOR_ACCENT      0x07E0  // Terminal Green
#define COLOR_TEXT        0xFFFF  // Pure White
#define COLOR_MUTED       0xAD55  // Gray Muted
#define COLOR_SELECTED    0xFD20  // Amber Orange Highlight
#define COLOR_SUCCESS     0x07E0  // Green
#define COLOR_DANGER      0xF800  // Red

enum UIState {
  STATE_MAIN_MENU = 0,
  STATE_WIFI_CONFIG,
  STATE_SSH_CONFIG,
  STATE_BT_SCAN,
  STATE_SSH_TERMINAL
};

class DisplayUI {
private:
  Adafruit_ST7789* tft = nullptr;
  UIState currentState = STATE_MAIN_MENU;

  // Main menu navigation
  int selectedMenuItem = 0;
  const int totalMenuItems = 4;
  const char* menuLabels[4] = {
    "1. Atur Koneksi WiFi",
    "2. Atur Target SSH Server",
    "3. Hubungkan ke SSH Server",
    "4. Pindai Bluetooth Keyboard"
  };

  // WiFi Config Form
  int wifiSelectedField = 0; // 0=SSID, 1=Pass, 2=Save, 3=Cancel
  String inputSSID;
  String inputWiFiPass;

  // SSH Config Form
  int sshSelectedField = 0; // 0=Host, 1=Port, 2=User, 3=Pass, 4=Save, 5=Cancel
  String inputSSHHost;
  String inputSSHPort;
  String inputSSHUser;
  String inputSSHPass;

  // Terminal Buffer (40 kolom x 28 baris)
  char termBuffer[TERM_ROWS][TERM_COLS];
  uint16_t termColor[TERM_ROWS][TERM_COLS];
  int termCursorRow = 0;
  int termCursorCol = 0;

  // Status flag
  bool needRedraw = true;

public:
  void begin() {
    // Inisialisasi SPI hardware VSPI (SCK=18, MOSI=23)
    SPI.begin(TFT_SCLK_PIN, -1, TFT_MOSI_PIN, TFT_CS_PIN);

    // Inisialisasi Display ST7789
    // Pin CS = -1 karena modul display tidak memiliki pin CS (hardwired ke GND)
    tft = new Adafruit_ST7789(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
    tft->init(SCREEN_WIDTH, SCREEN_HEIGHT, TFT_SPI_MODE);
    tft->setRotation(2); // Sesuaikan orientasi (0, 1, 2, atau 3)

    // Setup Backlight PWM jika pin valid (bukan GPIO 36)
    // Kompatibel dengan ESP32 Arduino Core 2.x dan Core 3.x
    if (TFT_BLK_PIN >= 0 && TFT_BLK_PIN != 36) {
#if (defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)) || (defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)))
      ledcAttach(TFT_BLK_PIN, TFT_PWM_FREQ, TFT_PWM_RES);
#else
      ledcSetup(TFT_PWM_CHANNEL, TFT_PWM_FREQ, TFT_PWM_RES);
      ledcAttachPin(TFT_BLK_PIN, TFT_PWM_CHANNEL);
#endif
      setBrightness(Storage.config.screenBrightness);
    } else {
      Serial.println("[TFT] CATATAN: BLK terpasang pada GPIO 36 atau diabaikan. GPIO 36 tidak mendukung PWM output!");
    }

    tft->fillScreen(COLOR_BG);
    clearTerminal();
    drawMainMenu();
  }

  void setBrightness(uint8_t val) {
    if (TFT_BLK_PIN >= 0 && TFT_BLK_PIN != 36) {
#if (defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)) || (defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)))
      ledcWrite(TFT_BLK_PIN, val);
#else
      ledcWrite(TFT_PWM_CHANNEL, val);
#endif
    }
  }

  UIState getState() const {
    return currentState;
  }

  void setState(UIState newState) {
    currentState = newState;
    tft->fillScreen(COLOR_BG);

    if (currentState == STATE_MAIN_MENU) {
      drawMainMenu();
    } else if (currentState == STATE_WIFI_CONFIG) {
      wifiSelectedField = 0;
      inputSSID = Storage.config.wifiSSID;
      inputWiFiPass = Storage.config.wifiPass;
      drawWiFiConfig();
    } else if (currentState == STATE_SSH_CONFIG) {
      sshSelectedField = 0;
      inputSSHHost = Storage.config.sshHost;
      inputSSHPort = String(Storage.config.sshPort);
      inputSSHUser = Storage.config.sshUser;
      inputSSHPass = Storage.config.sshPass;
      drawSSHConfig();
    } else if (currentState == STATE_SSH_TERMINAL) {
      clearTerminal();
      renderTerminal();
    }
  }

  // -------------------------------------------------------------
  // STATUS BAR HEADER (240x24 pixel)
  // -------------------------------------------------------------
  void drawHeader(const char* title) {
    tft->fillRect(0, 0, 240, 22, COLOR_PRIMARY);
    tft->setTextColor(COLOR_TEXT);
    tft->setTextSize(1);
    tft->setCursor(6, 7);
    tft->print(title);

    // Indikator WiFi
    tft->setCursor(140, 7);
    if (WiFi.status() == WL_CONNECTED) {
      tft->setTextColor(COLOR_SUCCESS);
      tft->print("WiFi OK");
    } else {
      tft->setTextColor(COLOR_DANGER);
      tft->print("WiFi X");
    }

    // Indikator BT
    tft->setCursor(195, 7);
    if (BTKeyboard.getIsConnected()) {
      tft->setTextColor(COLOR_SUCCESS);
      tft->print("BT OK");
    } else {
      tft->setTextColor(COLOR_MUTED);
      tft->print("BT X");
    }

    tft->drawFastHLine(0, 22, 240, COLOR_MUTED);
  }

  // -------------------------------------------------------------
  // MENU UTAMA
  // -------------------------------------------------------------
  void drawMainMenu() {
    tft->fillScreen(COLOR_BG);
    drawHeader("ESP32 SSH TERMINAL");

    // Info panel
    tft->fillRoundRect(10, 28, 220, 48, 6, COLOR_SURFACE);
    tft->setTextColor(COLOR_MUTED);
    tft->setTextSize(1);
    tft->setCursor(18, 34);
    tft->printf("Host: %s:%d", Storage.config.sshHost.c_str(), Storage.config.sshPort);
    tft->setCursor(18, 48);
    tft->printf("User: %s", Storage.config.sshUser.c_str());
    tft->setCursor(18, 62);
    if (WiFi.status() == WL_CONNECTED) {
      tft->setTextColor(COLOR_SUCCESS);
      tft->printf("IP: %s", WiFi.localIP().toString().c_str());
    } else {
      tft->setTextColor(COLOR_DANGER);
      tft->print("IP: Tidak Terhubung");
    }

    // Render Menu Items
    int startY = 86;
    for (int i = 0; i < totalMenuItems; i++) {
      int itemY = startY + (i * 32);
      bool isSelected = (i == selectedMenuItem);

      if (isSelected) {
        tft->fillRoundRect(10, itemY, 220, 26, 4, COLOR_SELECTED);
        tft->setTextColor(0x0000); // Black text on amber
      } else {
        tft->fillRoundRect(10, itemY, 220, 26, 4, COLOR_SURFACE);
        tft->setTextColor(COLOR_TEXT);
      }

      tft->setTextSize(1);
      tft->setCursor(20, itemY + 8);
      tft->print(menuLabels[i]);
    }

    // Footer Hint
    tft->setTextColor(COLOR_MUTED);
    tft->setTextSize(1);
    tft->setCursor(15, 225);
    tft->print("[Panah]: Pilih  [Enter]: Buka");
  }

  // -------------------------------------------------------------
  // FORM ATUR WIFI
  // -------------------------------------------------------------
  void drawWiFiConfig() {
    tft->fillScreen(COLOR_BG);
    drawHeader("PENGATURAN WIFI");

    // Field 0: SSID
    drawFormField(32, "WiFi SSID:", inputSSID, wifiSelectedField == 0);

    // Field 1: Password
    String maskedPass = "";
    for (size_t i = 0; i < inputWiFiPass.length(); i++) maskedPass += "*";
    drawFormField(86, "WiFi Password:", maskedPass, wifiSelectedField == 1);

    // Tombol Aksi
    drawButton(10, 160, 105, 28, "Simpan", wifiSelectedField == 2, COLOR_SUCCESS);
    drawButton(125, 160, 105, 28, "Batal (ESC)", wifiSelectedField == 3, COLOR_DANGER);

    // Hint
    tft->setTextColor(COLOR_MUTED);
    tft->setTextSize(1);
    tft->setCursor(10, 215);
    tft->print("[Panah]: Geser Form");
    tft->setCursor(10, 227);
    tft->print("[ESC]: Batal  [Enter]: Simpan");
  }

  // -------------------------------------------------------------
  // FORM ATUR TARGET SSH SERVER
  // -------------------------------------------------------------
  void drawSSHConfig() {
    tft->fillScreen(COLOR_BG);
    drawHeader("TARGET SSH SERVER");

    drawFormField(26, "SSH Host / IP:", inputSSHHost, sshSelectedField == 0);
    drawFormField(66, "Port (Default 22):", inputSSHPort, sshSelectedField == 1);
    drawFormField(106, "Username:", inputSSHUser, sshSelectedField == 2);

    String maskedPass = "";
    for (size_t i = 0; i < inputSSHPass.length(); i++) maskedPass += "*";
    drawFormField(146, "Password (opsional):", maskedPass, sshSelectedField == 3);

    // Tombol Save & Cancel
    drawButton(10, 195, 105, 26, "Simpan (Save)", sshSelectedField == 4, COLOR_SUCCESS);
    drawButton(125, 195, 105, 26, "Kembali (ESC)", sshSelectedField == 5, COLOR_DANGER);

    tft->setTextColor(COLOR_MUTED);
    tft->setTextSize(1);
    tft->setCursor(10, 228);
    tft->print("[ESC]: Kembali  [Enter]: Simpan");
  }

  void drawFormField(int y, const char* label, const String& value, bool selected) {
    tft->setTextSize(1);
    tft->setTextColor(COLOR_MUTED);
    tft->setCursor(12, y);
    tft->print(label);

    if (selected) {
      tft->drawRoundRect(10, y + 10, 220, 22, 3, COLOR_SELECTED);
      tft->fillRoundRect(11, y + 11, 218, 20, 3, COLOR_SURFACE);
      tft->setTextColor(COLOR_TEXT);
    } else {
      tft->drawRoundRect(10, y + 10, 220, 22, 3, COLOR_MUTED);
      tft->fillRoundRect(11, y + 11, 218, 20, 3, COLOR_BG);
      tft->setTextColor(COLOR_MUTED);
    }

    tft->setCursor(16, y + 16);
    if (value.length() == 0 && selected) {
      tft->setTextColor(COLOR_MUTED);
      tft->print("<ketik disini>");
    } else {
      tft->print(value);
      if (selected) tft->print("_"); // Blinking cursor visual
    }
  }

  void drawButton(int x, int y, int w, int h, const char* label, bool selected, uint16_t color) {
    if (selected) {
      tft->fillRoundRect(x, y, w, h, 4, COLOR_SELECTED);
      tft->setTextColor(0x0000);
    } else {
      tft->fillRoundRect(x, y, w, h, 4, color);
      tft->setTextColor(COLOR_TEXT);
    }
    tft->setTextSize(1);
    int textX = x + (w - (strlen(label) * 6)) / 2;
    tft->setCursor(textX, y + (h - 8) / 2);
    tft->print(label);
  }

  // -------------------------------------------------------------
  // TERMINAL ENGINE (VT100 & Text Grid Emulator)
  // -------------------------------------------------------------
  void clearTerminal() {
    for (int r = 0; r < TERM_ROWS; r++) {
      for (int c = 0; c < TERM_COLS; c++) {
        termBuffer[r][c] = ' ';
        termColor[r][c] = COLOR_ACCENT;
      }
    }
    termCursorRow = 0;
    termCursorCol = 0;
  }

  void renderTerminal() {
    tft->fillScreen(0x0000); // Black background
    for (int r = 0; r < TERM_ROWS; r++) {
      for (int c = 0; c < TERM_COLS; c++) {
        if (termBuffer[r][c] != ' ') {
          tft->drawChar(c * 6, r * 8, termBuffer[r][c], termColor[r][c], 0x0000, 1);
        }
      }
    }
    drawTerminalFooter();
  }

  void drawTerminalFooter() {
    tft->fillRect(0, 228, 240, 12, COLOR_SURFACE);
    tft->setTextColor(COLOR_MUTED);
    tft->setTextSize(1);
    tft->setCursor(4, 230);
    tft->print("[Right-CTRL + C]: Keluar Sesi SSH");
  }

  void scrollTerminalUp() {
    memmove(&termBuffer[0], &termBuffer[1], sizeof(termBuffer[0]) * (TERM_ROWS - 1));
    memmove(&termColor[0], &termColor[1], sizeof(termColor[0]) * (TERM_ROWS - 1));
    for (int c = 0; c < TERM_COLS; c++) {
      termBuffer[TERM_ROWS - 1][c] = ' ';
      termColor[TERM_ROWS - 1][c] = COLOR_ACCENT;
    }
    termCursorRow = TERM_ROWS - 1;
    renderTerminal();
  }

  void printTerminalChar(char ch, uint16_t color = COLOR_ACCENT) {
    if (ch == '\r') {
      termCursorCol = 0;
      return;
    }
    if (ch == '\n') {
      termCursorCol = 0;
      termCursorRow++;
      if (termCursorRow >= TERM_ROWS) {
        scrollTerminalUp();
      }
      return;
    }
    if (ch == '\b' || ch == 127) { // Backspace
      if (termCursorCol > 0) {
        termCursorCol--;
        termBuffer[termCursorRow][termCursorCol] = ' ';
        tft->drawChar(termCursorCol * 6, termCursorRow * 8, ' ', color, 0x0000, 1);
      }
      return;
    }
    if (ch == '\t') {
      termCursorCol = (termCursorCol + 4) & ~3;
      if (termCursorCol >= TERM_COLS) {
        termCursorCol = 0;
        termCursorRow++;
        if (termCursorRow >= TERM_ROWS) scrollTerminalUp();
      }
      return;
    }

    // Render Karakter Normal
    if (ch >= 32 && ch <= 126) {
      if (termCursorCol >= TERM_COLS) {
        termCursorCol = 0;
        termCursorRow++;
        if (termCursorRow >= TERM_ROWS) {
          scrollTerminalUp();
        }
      }
      termBuffer[termCursorRow][termCursorCol] = ch;
      termColor[termCursorRow][termCursorCol] = color;
      tft->drawChar(termCursorCol * 6, termCursorRow * 8, ch, color, 0x0000, 1);
      termCursorCol++;
    }
  }

  void printTerminalString(const String& str, uint16_t color = COLOR_ACCENT) {
    for (size_t i = 0; i < str.length(); i++) {
      printTerminalChar(str[i], color);
    }
  }

  // -------------------------------------------------------------
  // PEMROSESAN NAVIGASI DARI KEYBOARD BLUETOOTH
  // -------------------------------------------------------------
  void handleKeyInput(const KeyEvent& ev) {
    if (currentState == STATE_MAIN_MENU) {
      handleMenuKey(ev);
    } else if (currentState == STATE_WIFI_CONFIG) {
      handleWiFiConfigKey(ev);
    } else if (currentState == STATE_SSH_CONFIG) {
      handleSSHConfigKey(ev);
    }
  }

private:
  void handleMenuKey(const KeyEvent& ev) {
    if (ev.special == KEY_UP) {
      selectedMenuItem = (selectedMenuItem - 1 + totalMenuItems) % totalMenuItems;
      drawMainMenu();
    } else if (ev.special == KEY_DOWN) {
      selectedMenuItem = (selectedMenuItem + 1) % totalMenuItems;
      drawMainMenu();
    } else if (ev.special == KEY_ENTER) {
      switch (selectedMenuItem) {
        case 0: setState(STATE_WIFI_CONFIG); break;
        case 1: setState(STATE_SSH_CONFIG); break;
        case 2: setState(STATE_SSH_TERMINAL); break;
        case 3:
          // Pindai bluetooth keyboard (BLE Scan)
          Serial.println("\n[UI] Memulai pemindaian BLE Keyboard...");
          BTKeyboard.startScan(5);
          drawMainMenu();
          break;
      }
    }
  }

  void handleWiFiConfigKey(const KeyEvent& ev) {
    if (ev.special == KEY_ESC) {
      setState(STATE_MAIN_MENU);
      return;
    }
    if (ev.special == KEY_UP) {
      wifiSelectedField = (wifiSelectedField - 1 + 4) % 4;
      drawWiFiConfig();
      return;
    }
    if (ev.special == KEY_DOWN || ev.special == KEY_TAB) {
      wifiSelectedField = (wifiSelectedField + 1) % 4;
      drawWiFiConfig();
      return;
    }

    if (wifiSelectedField == 0) { // SSID
      handleTextInput(inputSSID, ev);
      drawWiFiConfig();
    } else if (wifiSelectedField == 1) { // Pass
      handleTextInput(inputWiFiPass, ev);
      drawWiFiConfig();
    } else if (wifiSelectedField == 2) { // Save
      if (ev.special == KEY_ENTER) {
        Storage.saveWiFi(inputSSID, inputWiFiPass);
        Serial.println("[UI] Pengaturan WiFi berhasil disimpan ke Flash!");
        setState(STATE_MAIN_MENU);
      }
    } else if (wifiSelectedField == 3) { // Cancel
      if (ev.special == KEY_ENTER) {
        setState(STATE_MAIN_MENU);
      }
    }
  }

  void handleSSHConfigKey(const KeyEvent& ev) {
    if (ev.special == KEY_ESC) {
      setState(STATE_MAIN_MENU);
      return;
    }
    if (ev.special == KEY_UP) {
      sshSelectedField = (sshSelectedField - 1 + 6) % 6;
      drawSSHConfig();
      return;
    }
    if (ev.special == KEY_DOWN || ev.special == KEY_TAB) {
      sshSelectedField = (sshSelectedField + 1) % 6;
      drawSSHConfig();
      return;
    }

    if (sshSelectedField == 0) {
      handleTextInput(inputSSHHost, ev);
      drawSSHConfig();
    } else if (sshSelectedField == 1) {
      handleTextInput(inputSSHPort, ev);
      drawSSHConfig();
    } else if (sshSelectedField == 2) {
      handleTextInput(inputSSHUser, ev);
      drawSSHConfig();
    } else if (sshSelectedField == 3) {
      handleTextInput(inputSSHPass, ev);
      drawSSHConfig();
    } else if (sshSelectedField == 4) { // Save
      if (ev.special == KEY_ENTER) {
        uint16_t port = inputSSHPort.toInt();
        if (port == 0) port = 22;
        Storage.saveSSH(inputSSHHost, port, inputSSHUser, inputSSHPass);
        Serial.println("[UI] Pengaturan SSH Server berhasil disimpan ke Flash!");
        setState(STATE_MAIN_MENU);
      }
    } else if (sshSelectedField == 5) { // Cancel
      if (ev.special == KEY_ENTER) {
        setState(STATE_MAIN_MENU);
      }
    }
  }

  void handleTextInput(String& str, const KeyEvent& ev) {
    if (ev.special == KEY_BACKSPACE) {
      if (str.length() > 0) {
        str.remove(str.length() - 1);
      }
    } else if (ev.ascii >= 32 && ev.ascii <= 126) {
      str += ev.ascii;
    }
  }
};

extern DisplayUI UI;

#endif // DISPLAY_UI_H
