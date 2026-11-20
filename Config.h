#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// KONFIGURASI PIN HARDWARE (ESP32 WROOM-32 & ST7789 240x240 IPS)
// ============================================================================
// Sesuai permintaan pengguna:
// • VCC         -> 3.3V
// • GND         -> GND
// • SCL (SCLK)  -> GPIO 18 (VSPI CLK)
// • SDA (MOSI)  -> GPIO 23 (VSPI MOSI)
// • RES / RST   -> GPIO 4
// • DC          -> GPIO 2
// • CS          -> TIDAK ADA (Hardwired ke GND di PCB LCD modul 7-pin, set -1)
//
// PERINGATAN KRITIS TERKAIT PIN BLK / BACKLIGHT (GPIO 36):
// Pada ESP32 WROOM-32, GPIO 36 (SENSOR_VP), GPIO 39 (SENSOR_VN), GPIO 34,
// dan GPIO 35 adalah pin KHUSUS INPUT (GPI - General Purpose Input).
// Pin ini TIDAK MEMILIKI transistor output driver pada silikon ESP32!
// Oleh karena itu, GPIO 36 TIDAK DAPAT menghasilkan sinyal digital output
// maupun sinyal PWM (ledc) untuk mengatur kecerahan backlight.
//
// SOLUSI:
// 1. Sangat direkomendasikan memindahkan kabel BLK ke GPIO output seperti GPIO 32 atau 33.
// 2. Atau sambungkan pin BLK langsung ke 3.3V (Backlight selalu aktif 100%).
// Jika Anda tetap mendefinisikan GPIO 36, backlight tidak akan menyala via software!
// ============================================================================

#define TFT_SCLK_PIN    18
#define TFT_MOSI_PIN    23
#define TFT_RST_PIN      4
#define TFT_DC_PIN       2
#define TFT_CS_PIN      -1  // Modul tanpa pin CS (CS internal terhubung ke GND)

// Opsi Backlight:
// Default disarankan ke GPIO 32 untuk kontrol PWM kecerahan layar.
// Jika ingin mencoba GPIO 36 (meski input only), ganti nilainya ke 36.
// Jika dihubungkan langsung ke 3.3V, ubah ke -1.
#define TFT_BLK_PIN     14  
#define TFT_PWM_CHANNEL  0
#define TFT_PWM_FREQ  5000
#define TFT_PWM_RES      8  // Resolusi 8-bit (0 - 255)

// Dimensi Layar ST7789
#define SCREEN_WIDTH   240
#define SCREEN_HEIGHT  240

// Terminal Grid (Karakter font 6x8 pixels)
#define TERM_COLS       40  // 240 / 6 = 40 kolom
#define TERM_ROWS       28  // 240 / 8 = 30 baris (disediakan 2 baris untuk status bar)

// Default SSH & WiFi Settings
#define DEFAULT_SSH_PORT      22
#define DEFAULT_SSH_USER      "pi"
#define DEFAULT_SSH_HOST      "192.168.1.100"

// Timeout & Buffer Limits
#define SSH_RX_BUFFER_SIZE  1024
#define SERIAL_BAUD_RATE  115200

#endif // CONFIG_H
