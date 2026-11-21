#ifndef SSH_CLIENT_MANAGER_H
#define SSH_CLIENT_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"
#include "StorageManager.h"
#include "DisplayUI.h"

// Library LibSSH-ESP32 (Tersedia di Library Manager Arduino IDE)
#include <libssh_esp32.h>
#include <libssh/libssh.h>
#define HAS_LIBSSH 1

class SSHClientManager {
private:
  bool isConnected = false;
  bool isConnecting = false;

#if HAS_LIBSSH
  ssh_session session = nullptr;
  ssh_channel channel = nullptr;
#endif

public:
  bool getIsConnected() const {
    return isConnected;
  }

  void begin() {
#if HAS_LIBSSH
    libssh_begin();
    Serial.println("[SSH] LibSSH-ESP32 siap digunakan.");
#else
    Serial.println("[SSH] CATATAN: Header 'libssh_esp32.h' belum terpasang di Arduino IDE.");
    Serial.println("[SSH] Silakan instal library 'LibSSH-ESP32' via Library Manager Arduino IDE.");
#endif
  }

  bool ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;

    if (Storage.config.wifiSSID.length() == 0) {
      Serial.println("[WIFI] SSID belum diatur! Buka menu 'Atur Koneksi WiFi'.");
      UI.printTerminalString("\n[ERROR] SSID WiFi belum diatur!\nAtur via Menu LCD atau Serial.\n", COLOR_DANGER);
      return false;
    }

    Serial.printf("[WIFI] Menghubungkan ke %s...\n", Storage.config.wifiSSID.c_str());
    UI.printTerminalString("\nMenghubungkan ke WiFi: ", COLOR_SELECTED);
    UI.printTerminalString(Storage.config.wifiSSID + "\n", COLOR_TEXT);

    WiFi.disconnect();
    delay(100);
    WiFi.begin(Storage.config.wifiSSID.c_str(), Storage.config.wifiPass.c_str());

    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 25) {
      delay(500);
      Serial.print(".");
      UI.printTerminalChar('.', COLOR_SELECTED);
      timeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\n[WIFI] Terhubung! IP: %s\n", WiFi.localIP().toString().c_str());
      UI.printTerminalString("\n[OK] WiFi Terhubung! IP: ", COLOR_SUCCESS);
      UI.printTerminalString(WiFi.localIP().toString() + "\n", COLOR_TEXT);
      return true;
    } else {
      Serial.println("\n[WIFI] Gagal terhubung ke WiFi!");
      UI.printTerminalString("\n[GAGAL] WiFi tidak dapat terhubung!\n", COLOR_DANGER);
      return false;
    }
  }

  bool startSession() {
    UI.clearTerminal();
    UI.drawTerminalFooter();
    UI.printTerminalString("=== MEMBUKA SESI SSH ===\n", COLOR_SELECTED);

    if (!ensureWiFi()) {
      delay(2000);
      return false;
    }

    UI.printTerminalString("Target: ", COLOR_MUTED);
    UI.printTerminalString(Storage.config.sshUser + "@" + Storage.config.sshHost + ":" + String(Storage.config.sshPort) + "\n", COLOR_TEXT);

#if HAS_LIBSSH
    if (session != nullptr) {
      closeSession();
    }

    session = ssh_new();
    if (session == nullptr) {
      UI.printTerminalString("[ERROR] Gagal alokasi ssh_session!\n", COLOR_DANGER);
      return false;
    }

    int port = Storage.config.sshPort;
    long timeout = 10;
    ssh_options_set(session, SSH_OPTIONS_HOST, Storage.config.sshHost.c_str());
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(session, SSH_OPTIONS_USER, Storage.config.sshUser.c_str());
    ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);

    UI.printTerminalString("Menghubungkan ke server...\n", COLOR_MUTED);
    int rc = ssh_connect(session);
    if (rc != SSH_OK) {
      UI.printTerminalString("[ERROR] Gagal konek: ", COLOR_DANGER);
      UI.printTerminalString(String(ssh_get_error(session)) + "\n", COLOR_DANGER);
      ssh_free(session);
      session = nullptr;
      return false;
    }

    UI.printTerminalString("Autentikasi...\n", COLOR_MUTED);
    if (Storage.config.sshPass.length() > 0) {
      rc = ssh_userauth_password(session, NULL, Storage.config.sshPass.c_str());
    } else {
      rc = ssh_userauth_none(session, NULL);
    }

    if (rc != SSH_AUTH_SUCCESS) {
      UI.printTerminalString("[ERROR] Autentikasi gagal! Cek user/password.\n", COLOR_DANGER);
      closeSession();
      return false;
    }

    UI.printTerminalString("[OK] Autentikasi Berhasil!\n", COLOR_SUCCESS);

    // Buka Channel & Interactive Shell (PTY)
    channel = ssh_channel_new(session);
    if (channel == nullptr || ssh_channel_open_session(channel) != SSH_OK) {
      UI.printTerminalString("[ERROR] Gagal membuka channel SSH!\n", COLOR_DANGER);
      closeSession();
      return false;
    }

    // Request PTY terminal ukuran 40 cols x 28 rows
    rc = ssh_channel_request_pty_size(channel, "vt100", TERM_COLS, TERM_ROWS);
    if (rc != SSH_OK) {
      UI.printTerminalString("[WARN] Request PTY ditolak, mencoba default...\n", COLOR_MUTED);
    }

    rc = ssh_channel_request_shell(channel);
    if (rc != SSH_OK) {
      UI.printTerminalString("[ERROR] Gagal membuka shell interaktif!\n", COLOR_DANGER);
      closeSession();
      return false;
    }

    isConnected = true;
    UI.printTerminalString("--- TERMINAL READY ---\n", COLOR_ACCENT);
    Serial.println("[SSH] Sesi interaktif berhasil dimulai!");
    return true;

#else
    // Fallback Mock Terminal untuk keperluan uji coba jika LibSSH belum diinstal
    UI.printTerminalString("\n[CATATAN] Library LibSSH-ESP32 belum ada.\n", COLOR_SELECTED);
    UI.printTerminalString("Silakan pasang 'LibSSH-ESP32' di Arduino IDE.\n", COLOR_MUTED);
    UI.printTerminalString("Mode Simulasi Terminal Aktif.\n", COLOR_ACCENT);
    UI.printTerminalString("Ketik perintah apa saja via BT Keyboard.\n", COLOR_TEXT);
    UI.printTerminalString("Tekan Right-CTRL+C untuk kembali ke Menu.\n\n", COLOR_SELECTED);
    UI.printTerminalString("esp32@terminal:~$ ", COLOR_SUCCESS);
    isConnected = true;
    return true;
#endif
  }

  void loop() {
    if (!isConnected) return;

#if HAS_LIBSSH
    if (channel != nullptr && ssh_channel_is_open(channel)) {
      char buf[256];
      int nbytes = ssh_channel_read_nonblocking(channel, buf, sizeof(buf) - 1, 0);
      if (nbytes > 0) {
        buf[nbytes] = '\0';
        for (int i = 0; i < nbytes; i++) {
          UI.printTerminalChar(buf[i], COLOR_TEXT);
          Serial.write(buf[i]);
        }
      } else if (nbytes < 0 && ssh_channel_is_eof(channel)) {
        UI.printTerminalString("\n[SSH] Server menutup koneksi.\n", COLOR_DANGER);
        closeSession();
      }
    }
#endif
  }

  void sendInput(const char* data, size_t len) {
    if (!isConnected) return;

#if HAS_LIBSSH
    if (channel != nullptr && ssh_channel_is_open(channel)) {
      ssh_channel_write(channel, data, len);
    }
#else
    // Echo di mode simulasi
    for (size_t i = 0; i < len; i++) {
      UI.printTerminalChar(data[i], COLOR_ACCENT);
      if (data[i] == '\r') {
        UI.printTerminalChar('\n', COLOR_ACCENT);
        UI.printTerminalString("esp32@terminal:~$ ", COLOR_SUCCESS);
      }
    }
#endif
  }

  void closeSession() {
    if (!isConnected) return;
    Serial.println("[SSH] Menutup sesi SSH...");
    UI.printTerminalString("\n[SSH] Sesi ditutup.\n", COLOR_MUTED);

#if HAS_LIBSSH
    if (channel != nullptr) {
      ssh_channel_send_eof(channel);
      ssh_channel_close(channel);
      ssh_channel_free(channel);
      channel = nullptr;
    }
    if (session != nullptr) {
      ssh_disconnect(session);
      ssh_free(session);
      session = nullptr;
    }
#endif

    isConnected = false;
    delay(1000);
    UI.setState(STATE_MAIN_MENU);
  }
};

extern SSHClientManager SSHClient;

#endif // SSH_CLIENT_MANAGER_H
