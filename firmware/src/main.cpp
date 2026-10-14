// Claude Token Monitor — Fase 1 (MVP)
// LILYGO T-Dongle-C5: recibe por USB Serial/JTAG una línea JSON por actualización
// y muestra el total de tokens del día en la TFT.
//
// Protocolo (una línea por mensaje, terminada en '\n'):
//   {"v":1,"day_tokens":591725,"date":"2026-10-14"}
// Las claves desconocidas se ignoran, así las fases siguientes pueden
// agregar campos sin romper esta versión.

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <ArduinoJson.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>

#include "pin_config.h"

// Orientación horizontal (160x80) con el conector USB a la izquierda, igual que
// el ejemplo factory de LilyGO (su rotación 3 = MADCTL MY|MV = rotación 1 de Adafruit).
// Si la ves dada vuelta, cambiá a 3.
#define TFT_ROTATION 1

// Si no llega nada en este tiempo, avisamos en pantalla que el daemon no responde.
static const uint32_t STALE_AFTER_MS = 35 * 1000;
static const size_t RX_LINE_MAX = 512;

SPIClass &lcdSpi = SPI;
Adafruit_ST7735 tft(&lcdSpi, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);
// Dibujamos en un framebuffer en RAM y lo mandamos entero: cero parpadeo.
GFXcanvas16 canvas(TFT_HEIGHT, TFT_WIDTH);  // 160x80 en horizontal

struct State {
  bool hasData = false;
  uint64_t dayTokens = 0;
  char date[11] = "";        // "YYYY-MM-DD"
  uint32_t lastRxMs = 0;
  uint32_t parseErrors = 0;
} state;

static char lineBuf[RX_LINE_MAX + 1];
static size_t lineLen = 0;
static bool lineOverflow = false;

// ---------------------------------------------------------------- formato

// 591725 -> "591.725" (separador de miles a la argentina)
static void formatThousands(uint64_t n, char *out, size_t outSize) {
  char digits[24];
  snprintf(digits, sizeof(digits), "%llu", (unsigned long long)n);
  size_t len = strlen(digits);
  size_t o = 0;
  for (size_t i = 0; i < len && o + 1 < outSize; i++) {
    if (i > 0 && (len - i) % 3 == 0 && o + 2 < outSize) out[o++] = '.';
    out[o++] = digits[i];
  }
  out[o] = '\0';
}

// 12345678 -> "12,3M" (por si no entra ni con la fuente más chica)
static void formatCompact(uint64_t n, char *out, size_t outSize) {
  const char *suffix[] = {"", "K", "M", "B", "T"};
  double v = (double)n;
  int s = 0;
  while (v >= 1000.0 && s < 4) {
    v /= 1000.0;
    s++;
  }
  snprintf(out, outSize, v < 100.0 ? "%.1f%s" : "%.0f%s", v, suffix[s]);
  for (char *p = out; *p; p++)
    if (*p == '.') *p = ',';
}

// ---------------------------------------------------------------- dibujo

static int16_t textWidth(const char *s) {
  int16_t x1, y1;
  uint16_t w, h;
  canvas.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return (int16_t)w;
}

// Dibuja `s` centrado horizontalmente con la fuente más grande que entre.
static void drawBigNumber(const char *s, int16_t baselineY) {
  static const GFXfont *fonts[] = {&FreeSansBold18pt7b, &FreeSansBold12pt7b,
                                   &FreeSansBold9pt7b};
  const int16_t maxW = canvas.width() - 4;
  for (const GFXfont *f : fonts) {
    canvas.setFont(f);
    int16_t w = textWidth(s);
    if (w <= maxW || f == &FreeSansBold9pt7b) {
      int16_t x1, y1;
      uint16_t bw, bh;
      canvas.getTextBounds(s, 0, 0, &x1, &y1, &bw, &bh);
      canvas.setCursor((canvas.width() - (int16_t)bw) / 2 - x1, baselineY);
      canvas.print(s);
      break;
    }
  }
  canvas.setFont(nullptr);
}

static void drawSmallRight(const char *s, int16_t y, uint16_t color) {
  canvas.setFont(nullptr);
  canvas.setTextSize(1);
  canvas.setTextColor(color);
  canvas.setCursor(canvas.width() - (int16_t)strlen(s) * 6 - 2, y);
  canvas.print(s);
}

static void render() {
  canvas.fillScreen(ST77XX_BLACK);
  canvas.setTextWrap(false);

  // Cabecera
  canvas.setFont(nullptr);
  canvas.setTextSize(1);
  canvas.setTextColor(0xAD55);  // gris claro
  canvas.setCursor(2, 2);
  canvas.print("TOKENS HOY");
  if (state.hasData && strlen(state.date) == 10) {
    char dm[6];  // "DD/MM"
    snprintf(dm, sizeof(dm), "%.2s/%.2s", state.date + 8, state.date + 5);
    drawSmallRight(dm, 2, 0xAD55);
  }
  canvas.drawFastHLine(0, 12, canvas.width(), 0x4208);

  // Número
  canvas.setTextColor(ST77XX_WHITE);
  if (state.hasData) {
    char num[32];
    formatThousands(state.dayTokens, num, sizeof(num));
    canvas.setFont(&FreeSansBold9pt7b);
    if (textWidth(num) > canvas.width() - 4) formatCompact(state.dayTokens, num, sizeof(num));
    drawBigNumber(num, 54);
  } else {
    canvas.setFont(&FreeSansBold9pt7b);
    const char *msg = "esperando PC";
    canvas.setCursor((canvas.width() - textWidth(msg)) / 2, 46);
    canvas.print(msg);
    canvas.setFont(nullptr);
  }

  // Pie: antigüedad del dato
  canvas.setFont(nullptr);
  canvas.setTextSize(1);
  char foot[32];
  uint16_t footColor = 0x8410;
  if (!state.hasData) {
    snprintf(foot, sizeof(foot), "corre el daemon :)");
  } else {
    uint32_t age = (millis() - state.lastRxMs) / 1000;
    if (millis() - state.lastRxMs > STALE_AFTER_MS) {
      snprintf(foot, sizeof(foot), "SIN DATOS hace %lus", (unsigned long)age);
      footColor = ST77XX_RED;
    } else {
      snprintf(foot, sizeof(foot), "actualizado hace %lus", (unsigned long)age);
    }
  }
  canvas.setTextColor(footColor);
  canvas.setCursor((canvas.width() - (int16_t)strlen(foot) * 6) / 2, 70);
  canvas.print(foot);

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), canvas.width(), canvas.height());
}

// ---------------------------------------------------------------- serial

static void handleLine(const char *line) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    state.parseErrors++;
    Serial.printf("ERR parse: %s\n", err.c_str());
    return;
  }
  JsonVariant tokens = doc["day_tokens"];
  if (!tokens.is<uint64_t>()) {
    state.parseErrors++;
    Serial.println("ERR falta day_tokens");
    return;
  }
  state.dayTokens = tokens.as<uint64_t>();
  const char *date = doc["date"] | "";
  strlcpy(state.date, date, sizeof(state.date));
  state.hasData = true;
  state.lastRxMs = millis();
}

// Lee bytes disponibles y procesa líneas completas. Devuelve true si llegó algo válido.
static bool pollSerial() {
  bool updated = false;
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') {
      if (!lineOverflow && lineLen > 0) {
        lineBuf[lineLen] = '\0';
        uint32_t before = state.lastRxMs;
        handleLine(lineBuf);
        updated |= state.lastRxMs != before;
      }
      lineLen = 0;
      lineOverflow = false;
      continue;
    }
    if (lineLen < RX_LINE_MAX) {
      lineBuf[lineLen++] = (char)c;
    } else {
      lineOverflow = true;  // línea demasiado larga: se descarta entera
    }
  }
  return updated;
}

// ---------------------------------------------------------------- main

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, LOW);  // backlight on (activo en bajo)

  lcdSpi.begin(PIN_LCD_SCK, -1, PIN_LCD_MOSI, -1);
  // INITR_MINI160x80_PLUGIN = offsets 26/1, inversión ON y orden BGR:
  // exactamente lo que configura el driver st7735 de LilyGO para este panel.
  tft.initR(INITR_MINI160x80_PLUGIN);
  tft.setSPISpeed(27000000);
  tft.setRotation(TFT_ROTATION);
  tft.fillScreen(ST77XX_BLACK);

  render();
  Serial.println("claude-token-monitor fase1 listo");
}

void loop() {
  static uint32_t lastRenderMs = 0;
  bool updated = pollSerial();
  // Redibujamos al recibir datos y una vez por segundo para el contador de antigüedad.
  if (updated || millis() - lastRenderMs >= 1000) {
    render();
    lastRenderMs = millis();
  }
  delay(10);
}
