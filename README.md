# Proyek ESP32 Wroom-32: SSH Client Portabel dengan LCD IPS 240x240 (ST7789) & Bluetooth Keyboard Host

Proyek ini mengimplementasikan terminal **SSH Client interaktif portabel** berbasis **ESP32 Wroom-32** yang dilengkapi dengan layar **LCD IPS 240x240 (ST7789 SPI)** dan **Bluetooth Keyboard Host (BLE HID)**. Konfigurasi disimpan secara permanen di Flash internal ESP32 (NVS / Non-Volatile Storage).

---

## 1. Analisis Kapabilitas & Kelayakan Sistem

### A. Apakah ESP32 Wroom-32 bisa terhubung ke Bluetooth Keyboard?
**YA, SANGAT BISA.**
ESP32 Wroom-32 memiliki radio 2.4 GHz dual-mode (Bluetooth Classic BR/EDR dan BLE 4.2). Keyboard nirkabel Bluetooth di pasaran umumnya menggunakan profil **BLE HID (Human Interface Device over GATT / HOGP)**. Dengan menggunakan library **NimBLE-Arduino** (atau ESP-IDF HID Host), ESP32 bertindak sebagai **Central/Host (Client)** yang dapat memindai, memasangkan (*bonding & encryption*), dan membaca *scancode* ketikan tombol secara realtime dengan latensi sangat rendah.

### B. Apakah RAM dan Flash Cukup untuk SSH Client + ST7789 + Bluetooth?
**YA, SANGAT CUKUP**, dengan optimasi arsitektur berikut:
1. **Flash (ROM):**
   - ESP32 Wroom-32 memiliki Flash standar **4 MB**.
   - Gabungan stack WiFi, NimBLE, LibSSH (kriptografi mbedTLS), dan driver grafis memakan memori biner sekitar **1.4 MB - 1.8 MB**.
   - Pada Arduino IDE, cukup atur **Partition Scheme** ke: `"Huge APP (3MB No OTA/1MB SPIFFS)"`.
2. **RAM (SRAM):**
   - ESP32 memiliki SRAM 520 KB (~300 KB usable heap).
   - Penggunaan RAM:
     - NimBLE-Arduino: ~25-35 KB (jauh lebih hemat daripada Classic Bluedroid yang makan ~80 KB).
     - WiFi & LwIP: ~40-50 KB.
     - LibSSH (kriptografi Diffie-Hellman / AES): ~40-60 KB saat sesi aktif.
     - Layar ST7789: Menggunakan **Text Grid Emulator** (40 kolom x 28 baris = ~1.2 KB text buffer + ~2.4 KB color buffer = **total hanya 3.6 KB RAM**!), **TIDAK** membutuhkan framebuffer bitmap penuh (115 KB) sehingga menghemat sisa RAM secara masif.
     - Sisa Free Heap yang tersedia masih di kisaran **~100 KB - 140 KB**, sangat stabil untuk lalu lintas paket jaringan.

---

## 2. Peringatan Hardware Kritis: Pin GPIO 36 (BLK / Backlight)

> [!CAUTION]
> **PERINGATAN SANGAT PENTING MENGENAI GPIO 36:**
> Pada arsitektur silikon ESP32, pin **GPIO 34, 35, 36 (VP), dan 39 (VN)** adalah pin **KHUSUS INPUT (GPI - General Purpose Input)**.
> Pin-pin ini **TIDAK MEMILIKI TRANSISTOR OUTPUT DRIVER** maupun kanal PWM LEDC pada chip ESP32.
> 
> Jika pin **BLK** dihubungkan ke GPIO 36:
> - Software ESP32 tidak dapat mengeluarkan tegangan HIGH maupun sinyal PWM untuk menyalakan lampu latar (backlight).
> - Layar LCD akan tetap gelap/mati kecuali pin BLK menerima tegangan secara terpisah.
>
> **SOLUSI REKOMENDASI:**
> 1. Pindahkan kabel **BLK** ke pin output PWM lain, sangat disarankan ke **GPIO 32** (sudah disiapkan di `Config.h`), atau GPIO 33, GPIO 27, GPIO 21, GPIO 22.
> 2. **Atau** langsung hubungkan kabel **BLK** ke pin **3.3V** (Backlight akan menyala 100% secara permanen).

---

## 3. Diagram Pengkabelan (Wiring Pinout)

| Pin Modul ST7789 | Pin ESP32 Wroom-32 | Catatan Fungsi |
|---|---|---|
| **VCC** | **3.3V** | Catu daya modul LCD |
| **GND** | **GND** | Ground |
| **SCL / SCLK** | **GPIO 18** | Hardware VSPI Clock |
| **SDA / MOSI** | **GPIO 23** | Hardware VSPI Data Out |
| **RES / RST** | **GPIO 4** | Reset Hardware Display |
| **DC** | **GPIO 2** | Data / Command Selection |
| **CS** | *(TIDAK ADA)* | Sudah di-jumper ke GND internal di PCB modul (di software diset `-1`) |
| **BLK / BL** | **GPIO 14** *(atau GPIO 32 / 3.3V)* | **Jangan gunakan GPIO 36** karena GPIO 36 adalah pin *input-only* |

---

## 4. Persiapan Lingkungan Pengembangan (Arduino IDE)

### A. Pengaturan Board di Arduino IDE:
1. Buka menu **Tools > Board > ESP32 Arduino > ESP32 Dev Module**.
2. Buka menu **Tools > Partition Scheme** dan pilih:
   👉 **`Huge APP (3MB No OTA/1MB SPIFFS)`** (Wajib dipilih agar muat library SSH & Bluetooth).
3. Upload Speed: `921600` atau `115200`.

### B. Library yang Diperlukan (Instal via Library Manager):
Buka menu **Sketch > Include Library > Manage Libraries...**, lalu cari dan instal:
1. **`Adafruit GFX Library`** (oleh Adafruit)
2. **`Adafruit ST7789 and ST7735 Library`** (oleh Adafruit)
3. **`LibSSH-ESP32`** (oleh Ewan Parker)

*(Catatan: Bluetooth Dual-Mode Classic + BLE kini menggunakan stack native Bluedroid & `esp_hidh` yang sudah terpasang otomatis di dalam ESP32 Arduino Core, sehingga Anda tidak lagi memerlukan library NimBLE pihak ketiga!)*

---

## 5. Panduan Penggunaan & Alur Kerja

### A. Setup Awal Bluetooth Keyboard Dual-Mode (Classic & BLE) via Serial Monitor
Sistem ini menggunakan arsitektur **Native Bluetooth Dual-Mode (BTDM)** yang mendukung keyboard **Bluetooth Classic (BR/EDR)** maupun **Bluetooth Low Energy (BLE)** dalam satu firmware terpadu:
1. Buka **Serial Monitor** pada kecepatan **115200 baud**.
2. Nyalakan keyboard Bluetooth Anda dan masuk ke mode *Pairing* (biasanya lampu indikator berkedip cepat).
3. **Pilihan Perintah Scan:**
   * **`scan`** (atau ketik `1`): Menjalankan **Dual Scan Terpadu** (memindai Classic Bluetooth lalu BLE secara otomatis).
   * **`scan classic`**: Memindai khusus perangkat Bluetooth Classic (BR/EDR).
   * **`scan ble`**: Memindai khusus perangkat Bluetooth Low Energy (BLE).
4. ESP32 akan menampilkan hasil pemindaian terpadu:
   ```text
   ================ HASIL PEMINDAIAN BLUETOOTH ================
   No   | Tipe      | Nama Perangkat       | MAC Address        | RSSI 
   ------------------------------------------------------------
   [ 1] | [Classic] | Bluetooth Keyboard   | 11:21:AC:00:03:58  | -58 dBm
   [ 2] | [BLE]     | Logitech K380        | F4:73:35:12:34:56  | -68 dBm
   ============================================================
   ```
5. Untuk menghubungkan, ketik nomor index-nya:
   ```text
   select 1
   ```
6. **Injeksi MAC Manual (Sangat Cepat & Praktis):**
   Untuk keyboard Bluetooth Classic Anda yang memiliki MAC `11:21:AC:00:03:58`, Anda bisa langsung mengetik:
   ```text
   inject 11:21:AC:00:03:58
   ```
   *(Atau secara spesifik: `inject classic 11:21:AC:00:03:58` atau `inject ble <MAC>`)*.
7. **Pairing Passkey / PIN:**
   Jika keyboard Bluetooth Classic Anda meminta PIN atau Passkey, ESP32 akan menampilkannya di Serial Monitor:
   ```text
   >> PAIRING PASSKEY: KETIK [123456] LALU TEKAN ENTER DI KEYBOARD <<
   ```
   Cukup ketik angka tersebut di keyboard Bluetooth Anda lalu tekan tombol **Enter**.
8. MAC Address dan tipe protokol (Classic atau BLE) akan **otomatis tersimpan di Flash internal ESP32 (NVS)**. Pada saat dinyalakan berikutnya, ESP32 akan langsung otomatis terhubung tanpa perlu membuka Serial Monitor lagi!

---

### B. Navigasi Menu pada Layar LCD IPS 240x240

Pada layar LCD ST7789, terdapat UI visual dengan tema slate/navy modern:
1. **Header Status Bar:** Menampilkan status koneksi WiFi (`WiFi OK` / `WiFi X`) dan status Bluetooth (`BT OK` / `BT X`).
2. **Menu Utama:**
   - **`1. Atur Koneksi WiFi`**: Membuka form konfigurasi SSID dan Password WiFi.
   - **`2. Atur Target SSH Server`**: Membuka form konfigurasi Host/IP, Port, Username, dan Password target SSH.
   - **`3. Hubungkan ke SSH Server`**: Membuka koneksi WiFi, menginisialisasi sesi SSH interaktif, dan menampilkan layar terminal VT100.
   - **`4. Pindai Bluetooth Keyboard`**: Memulai ulang pemindaian keyboard BLE.

#### Kontrol Navigasi Tombol Keyboard:
- **Panah Atas / Bawah (`Up` / `Down Arrow`)**: Memindahkan pilihan menu atau berpindah antar field formulir.
- **`Enter`**: Membuka menu, memilih tombol *Simpan*, atau mengirim perintah di terminal.
- **`ESC`**: Keluar dari formulir pengaturan (WiFi / SSH) dan kembali ke Menu Utama.
- **`Backspace` / Karakter biasa**: Menghapus dan mengetik teks pada form input.

---

### C. Keluar dari Sesi Terminal SSH Aktif

Saat berada di dalam sesi SSH interaktif Linux:
- Tombol **`Ctrl + C` biasa** (Left Ctrl + C) akan mengirim karakter `SIGINT` (`\x03`) ke server Linux (berguna untuk menghentikan program yang sedang berjalan seperti `top`, `ping`, atau script bash).
- Untuk **KELUAR SEPENUHNYA DARI SESI SSH** dan kembali ke Menu LCD:
  👉 Tekan kombinasi: **`Right-CTRL + C`** (Tombol Control sebelah kanan + C).
  ESP32 akan langsung menutup sesi SSH, memutuskan channel PTY, dan mengembalikan tampilan layar ke Menu Utama.

---

## 6. Struktur File Proyek

- **`esp32-wroom32-SSH-240_240IPS.ino`**: File utama yang mengatur siklus hidup sistem, *event loop*, Serial CLI, dan integrasi antar modul.
- **`Config.h`**: Definisi pinout hardware, resolusi layar, channel PWM backlight, dan catatan teknis GPIO 36.
- **`StorageManager.h`**: Manajemen penyimpanan NVS flash (`Preferences.h`) untuk menyimpan SSID, Password, SSH IP/Port/User/Pass, dan MAC Bluetooth.
- **`BTKeyboardHost.h`**: Implementasi BLE HID Host menggunakan NimBLE, *scanning*, injeksi MAC, pemetaan *scancode* USB HID ke ASCII, serta deteksi pintasan `Right-Ctrl + C`.
- **`DisplayUI.h`**: Driver grafis ST7789, visualisasi menu modern, form konfigurasi, dan Text Grid Terminal VT100 Emulator berukuran 40 kolom x 28 baris.
- **`SSHClientManager.h`**: Engine koneksi LibSSH-ESP32, pembukaan channel PTY interaktif non-blocking, serta *fallback test mode*.
