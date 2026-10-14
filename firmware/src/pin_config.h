#pragma once

// Pinout del LILYGO T-Dongle-C5, confirmado contra el repo oficial:
//   github.com/Xinyuan-LilyGO/T-Dongle-C5  (include/pin_config.h + tabla del readme)
// NO son los pines del T-Dongle-S3.

// --- LCD ST7735 0.96" 80x160 (SPI) ---
#define PIN_LCD_MOSI 2
#define PIN_LCD_MISO 7  // compartido con SD_D0; la pantalla no lo usa
#define PIN_LCD_SCK 6
#define PIN_LCD_CS 10
#define PIN_LCD_DC 3   // "LCD_RS" en el readme de LilyGO
#define PIN_LCD_RST 1
#define PIN_LCD_BL 0   // backlight ACTIVO EN BAJO (LOW = encendido)

#define TFT_WIDTH 80
#define TFT_HEIGHT 160

// --- LED RGB: APA102 (reloj + datos), NO es un WS2812B ---
// Se usa recién en la Fase 2.
#define PIN_LED_CI 4  // clock
#define PIN_LED_DI 5  // data

// --- Varios ---
#define PIN_BOOT_BTN 28
#define PIN_SD_CS 23
