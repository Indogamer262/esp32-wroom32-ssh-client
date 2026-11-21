#ifndef SSH_CLIENT_MANAGER_H
#define SSH_CLIENT_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "Config.h"
#include "StorageManager.h"
#include "DisplayUI.h"

// Library LibSSH-ESP32 (Tersedia di Library Manager Arduino IDE)
#include <libssh_esp32.h>
#include <libssh/libssh.h>
#define HAS_LIBSSH 1

// Stack size yang optimal untuk operasi kriptografi LibSSH pada ESP32 (12 KB / 10 KB)
#define SSH_TASK_STACK_SIZE 10240
#define SSH_TASK_STACK_FALLBACK 8192

class SSHClientManager {
private:
  volatile bool isConnected = false;
  volatile bool isConnecting = false;
  volatile bool shouldStop = false;
  TaskHandle_t sshTaskHandle = nullptr;
  QueueHandle_t txQueue = nullptr;

  ssh_session session = nullptr;
  ssh_channel channel = nullptr;

public:
  bool getIsConnected() const {
    return isConnected;
  }

  bool getIsConnecting() const {
    return isConnecting;
  }

  void begin() {
    libssh_begin();
    if (txQueue == nullptr) {
      txQueue = xQueueCreate(256, sizeof(char));
    }
    Serial.println("[SSH] LibSSH-ESP32 Engine siap digunakan.");
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
    if (isConnecting || isConnected) return false;

    UI.clearTerminal();
    UI.drawTerminalFooter();
    UI.printTerminalString("=== MEMBUKA SESI SSH ===\n", COLOR_SELECTED);

    if (!ensureWiFi()) {
      delay(2000);
      return false;
    }

    isConnecting = true;
    shouldStop = false;

    // Tunggu task lama selesai dengan anggun jika masih berjalan
    if (sshTaskHandle != nullptr) {
      shouldStop = true;
      int waitLimit = 0;
      while (sshTaskHandle != nullptr && waitLimit < 10) {
        delay(100);
        waitLimit++;
      }
    }

    // Cek status memori kontigu
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    Serial.printf("[SSH] Status Memori -> Free Heap: %u bytes, Blok Kontigu Terbesar: %u bytes\n", freeHeap, largestBlock);

    // Buat Dedicated FreeRTOS Task (Coba 10 KB, jika fragmentasi tinggi fallback ke 8 KB)
    BaseType_t ret = xTaskCreatePinnedToCore(
      sshTaskStatic,
      "SSH_Worker",
      SSH_TASK_STACK_SIZE,
      this,
      2,
      &sshTaskHandle,
      1 // Jalankan pada Core 1 bersama UI
    );

    if (ret != pdPASS) {
      Serial.println("[SSH] Mencoba alokasi alternatif dengan stack 8 KB...");
      ret = xTaskCreatePinnedToCore(
        sshTaskStatic,
        "SSH_Worker",
        SSH_TASK_STACK_FALLBACK,
        this,
        2,
        &sshTaskHandle,
        1
      );
    }

    if (ret != pdPASS) {
      Serial.printf("[SSH] GAGAL: Memori kontigu tidak mencukupi (%u bytes tersisa)!\n", largestBlock);
      UI.printTerminalString("[ERROR] Gagal alokasi SSH Task!\nMemory Kurang.\n", COLOR_DANGER);
      isConnecting = false;
      delay(2000);
      UI.setState(STATE_MAIN_MENU);
      return false;
    }

    return true;
  }

  void loop() {
    // Sesi SSH kini dijalankan secara asinkron di dalam FreeRTOS Task (sshWorkerLoop)
  }

  void sendInput(const char* data, size_t len) {
    if (!isConnected || txQueue == nullptr || shouldStop) return;
    for (size_t i = 0; i < len; i++) {
      xQueueSend(txQueue, &data[i], 0);
    }
  }

  // Graceful shutdown yang AMAN dipanggil dari task mana pun (termasuk Bluetooth callback)
  void closeSession() {
    Serial.println("[SSH] Meminta penutupan sesi SSH secara aman...");
    shouldStop = true;
    isConnected = false;
    isConnecting = false;
    // Panggil ssh_disconnect pada session untuk membatalkan socket blocking di worker task
    if (session != nullptr) {
      ssh_disconnect(session);
    }
  }

private:
  static void sshTaskStatic(void* param) {
    SSHClientManager* self = (SSHClientManager*)param;
    self->runSSHWorker();
    self->sshTaskHandle = nullptr;
    vTaskDelete(NULL); // Task menghapus dirinya sendiri secara aman
  }

  void runSSHWorker() {
    String host = Storage.config.sshHost;
    unsigned int port = Storage.config.sshPort;
    String user = Storage.config.sshUser;
    String pass = Storage.config.sshPass;

    Serial.printf("\n[SSH] Target: %s@%s:%u\n", user.c_str(), host.c_str(), port);
    UI.printTerminalString("Target: ", COLOR_MUTED);
    UI.printTerminalString(user + "@" + host + ":" + String(port) + "\n", COLOR_TEXT);

    // 1. Alokasi Sesi LibSSH
    session = ssh_new();
    if (session == nullptr) {
      Serial.println("[SSH] Gagal alokasi ssh_session!");
      UI.printTerminalString("[ERROR] Gagal alokasi ssh_session!\n", COLOR_DANGER);
      cleanupWorker();
      return;
    }

    // 2. Konfigurasi Parameter LibSSH
    // Matikan pembacaan file konfigurasi Unix (/etc/ssh/ssh_config) yang tidak ada di ESP32
#ifdef SSH_OPTIONS_PROCESS_CONFIG
    int process_config = 0;
    ssh_options_set(session, SSH_OPTIONS_PROCESS_CONFIG, &process_config);
#endif

    long timeout = 8;
    int verbosity = SSH_LOG_PROTOCOL; // Aktifkan log protokol lengkap di Serial Monitor

    ssh_options_set(session, SSH_OPTIONS_HOST, host.c_str());
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(session, SSH_OPTIONS_USER, user.c_str());
    ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);
    ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);

    // 3. Buka Koneksi SSH Handshake Langsung (KEX & Cipher Negotiation)
    Serial.println("[SSH] Menjalankan ssh_connect (Key Exchange & Cipher Negotiation)...");
    UI.printTerminalString("Menghubungkan ke server...\n", COLOR_MUTED);

    int rc = ssh_connect(session);
    if (shouldStop) {
      cleanupWorker();
      return;
    }

    if (rc != SSH_OK) {
      const char* errStr = ssh_get_error(session);
      Serial.printf("[SSH] Gagal ssh_connect: %s (Kode: %d)\n", errStr, rc);
      UI.printTerminalString("[ERROR] Gagal konek: ", COLOR_DANGER);
      UI.printTerminalString(String(errStr) + "\n", COLOR_DANGER);
      cleanupWorker();
      return;
    }

    Serial.println("[SSH] Handshake KEX Berhasil! Melakukan Autentikasi...");
    UI.printTerminalString("Autentikasi...\n", COLOR_MUTED);

    // 4. Autentikasi User
    if (pass.length() > 0) {
      rc = ssh_userauth_password(session, NULL, pass.c_str());
    } else {
      rc = ssh_userauth_none(session, NULL);
    }

    if (shouldStop) {
      cleanupWorker();
      return;
    }

    if (rc != SSH_AUTH_SUCCESS) {
      Serial.printf("[SSH] Autentikasi Gagal! Kode: %d (Periksa Username & Password)\n", rc);
      UI.printTerminalString("[ERROR] Autentikasi gagal! Cek user/password.\n", COLOR_DANGER);
      cleanupWorker();
      return;
    }

    Serial.println("[SSH] Autentikasi Berhasil!");
    UI.printTerminalString("[OK] Autentikasi Berhasil!\n", COLOR_SUCCESS);

    // 5. Buka Channel & Interactive Shell (PTY VT100)
    channel = ssh_channel_new(session);
    if (channel == nullptr || ssh_channel_open_session(channel) != SSH_OK) {
      Serial.println("[SSH] Gagal membuka channel SSH!");
      UI.printTerminalString("[ERROR] Gagal membuka channel SSH!\n", COLOR_DANGER);
      cleanupWorker();
      return;
    }

    // Request terminal PTY ukuran 40 cols x 28 rows
    rc = ssh_channel_request_pty_size(channel, "vt100", TERM_COLS, TERM_ROWS);
    if (rc != SSH_OK) {
      Serial.println("[SSH] Peringatan: Request PTY size ditolak, mencoba request shell...");
    }

    rc = ssh_channel_request_shell(channel);
    if (rc != SSH_OK) {
      Serial.println("[SSH] Gagal membuka remote shell!");
      UI.printTerminalString("[ERROR] Gagal membuka shell interaktif!\n", COLOR_DANGER);
      cleanupWorker();
      return;
    }

    isConnected = true;
    isConnecting = false;
    UI.printTerminalString("--- TERMINAL READY ---\n", COLOR_ACCENT);
    Serial.println("\n[SSH] >> SESI TERMINAL INTERAKTIF SIAP DIGUNAKAN <<");

    // Kosongkan buffer txQueue sebelum mulai
    char dummy;
    while (xQueueReceive(txQueue, &dummy, 0) == pdTRUE);

    // 6. Loop Interaktif Non-blocking di dalam Task
    char buf[128];
    while (!shouldStop && isConnected && channel != nullptr && ssh_channel_is_open(channel)) {
      // Baca output dari SSH Server
      int nbytes = ssh_channel_read_nonblocking(channel, buf, sizeof(buf) - 1, 0);
      if (nbytes > 0) {
        buf[nbytes] = '\0';
        for (int i = 0; i < nbytes; i++) {
          UI.printTerminalChar(buf[i], COLOR_TEXT);
          Serial.write(buf[i]);
        }
      } else if (nbytes < 0 && ssh_channel_is_eof(channel)) {
        Serial.println("\n[SSH] Server menutup koneksi (EOF).");
        UI.printTerminalString("\n[SSH] Server menutup koneksi.\n", COLOR_DANGER);
        break;
      }

      // Kirim input keystroke dari Keyboard / Serial ke SSH Server
      char keyChar;
      while (xQueueReceive(txQueue, &keyChar, 0) == pdTRUE) {
        ssh_channel_write(channel, &keyChar, 1);
      }

      vTaskDelay(pdMS_TO_TICKS(15));
    }

    cleanupWorker();
  }

  // Cleanup aman yang berjalan di dalam thread worker sendiri
  void cleanupWorker() {
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

    isConnected = false;
    isConnecting = false;
    shouldStop = false;
    Serial.println("[SSH] Sesi SSH ditutup dengan bersih.");
    UI.printTerminalString("\n[SSH] Sesi ditutup.\n", COLOR_MUTED);
    vTaskDelay(pdMS_TO_TICKS(1500));
    UI.setState(STATE_MAIN_MENU);
  }
};

extern SSHClientManager SSHClient;

#endif // SSH_CLIENT_MANAGER_H
