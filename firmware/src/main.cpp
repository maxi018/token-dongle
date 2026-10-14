// Claude Token Monitor — LILYGO T-Dongle-C5
// Recibe por USB Serial/JTAG una línea JSON por actualización, muestra el total de
// tokens del día con Clawd animado al lado y usa el LED APA102 como semáforo.
//
// Protocolo (una línea por mensaje, terminada en '\n'):
//   {"v":1,"day_tokens":591725,"date":"2026-10-14","lvl":0.42}
// lvl = fracción del presupuesto diario consumida (la calcula el daemon; puede pasar de 1).
// Las claves desconocidas se ignoran.

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <ArduinoJson.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>

#include "clawd.h"
#include "led.h"
#include "pin_config.h"

// Orientación horizontal (160x80) con el conector USB a la izquierda, igual que
// el ejemplo factory de LilyGO (su rotación 3 = MADCTL MY|MV = rotación 1 de Adafruit).
// Si la ves dada vuelta, cambiá a 3.
#define TFT_ROTATION 1

// Si no llega nada en este tiempo, avisamos en pantalla que el daemon no responde.
static const uint32_t STALE_AFTER_MS = 35 * 1000;
// Cuánto tiempo Clawd sigue "trabajando" después de que suben los tokens.
static const uint32_t WORKING_FOR_MS = 12 * 1000;
static const uint32_t FRAME_MS = 50;  // ~20 fps
static const size_t RX_LINE_MAX = 512;

// Umbrales del semáforo (fracción del presupuesto diario)
static const float LVL_YELLOW = 0.6f;
static const float LVL_RED = 1.0f;

SPIClass &lcdSpi = SPI;
Adafruit_ST7735 tft(&lcdSpi, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);
// Dibujamos en un framebuffer en RAM y lo mandamos entero: cero parpadeo.
GFXcanvas16 canvas(TFT_HEIGHT, TFT_WIDTH);  // 160x80 en horizontal
Apa102Led led(PIN_LED_DI, PIN_LED_CI);

struct State {
  bool hasData = false;
  uint64_t dayTokens = 0;
  float lvl = -1.0f;         // < 0: el daemon no mandó presupuesto
  char date[11] = "";        // "YYYY-MM-DD"
  uint32_t lastRxMs = 0;
  uint32_t lastActivityMs = 0;
  bool everActive = false;
} state;

enum class LedMode : uint8_t { Normal, Dim, Off };
static LedMode ledMode = LedMode::Normal;

static char lineBuf[RX_LINE_MAX + 1];
static size_t lineLen = 0;
static bool lineOverflow = false;

static bool isStale(uint32_t now) { return now - state.lastRxMs > STALE_AFTER_MS; }

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

// 1640234 -> "1,64M", 12345678 -> "12,3M"
static void formatCompact(uint64_t n, char *out, size_t outSize) {
  const char *suffix[] = {"", "K", "M", "B", "T"};
  double v = (double)n;
  int s = 0;
  while (v >= 1000.0 && s < 4) {
    v /= 1000.0;
    s++;
  }
  const char *fmt = s == 0 ? "%.0f%s" : v < 10.0 ? "%.2f%s" : v < 100.0 ? "%.1f%s" : "%.0f%s";
  snprintf(out, outSize, fmt, v, suffix[s]);
  for (char *p = out; *p; p++)
    if (*p == '.') *p = ',';
}

// ---------------------------------------------------------------- dibujo

static uint16_t textWidth(const char *s) {
  int16_t x1, y1;
  uint16_t w, h;
  canvas.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return w;
}

// Elige la combinación (formato, fuente) más legible que entre en `maxW`
// y la dibuja centrada en [x0, x0+maxW].
static void drawTokens(uint64_t n, int16_t x0, int16_t maxW, int16_t baselineY) {
  char full[32], compact[16];
  formatThousands(n, full, sizeof(full));
  formatCompact(n, compact, sizeof(compact));
  struct Option {
    const char *text;
    const GFXfont *font;
  } options[] = {
      {full, &FreeSansBold18pt7b},    {full, &FreeSansBold12pt7b},
      {compact, &FreeSansBold18pt7b}, {compact, &FreeSansBold12pt7b},
      {full, &FreeSansBold9pt7b},     {compact, &FreeSansBold9pt7b},
  };
  for (const Option &o : options) {
    canvas.setFont(o.font);
    int16_t x1, y1;
    uint16_t w, h;
    canvas.getTextBounds(o.text, 0, 0, &x1, &y1, &w, &h);
    if (w <= maxW || o.font == &FreeSansBold9pt7b) {
      canvas.setCursor(x0 + (maxW - (int16_t)w) / 2 - x1, baselineY);
      canvas.print(o.text);
      break;
    }
  }
  canvas.setFont(nullptr);
}

static ClawdMood currentMood(uint32_t now) {
  if (!state.hasData || isStale(now)) return ClawdMood::Sleeping;
  if (state.lvl >= LVL_RED) return ClawdMood::Panic;
  if (state.everActive && now - state.lastActivityMs < WORKING_FOR_MS) return ClawdMood::Working;
  if (state.lvl >= LVL_YELLOW) return ClawdMood::Sweating;
  return ClawdMood::Idle;
}

static void render(uint32_t now) {
  canvas.fillScreen(ST77XX_BLACK);
  canvas.setTextWrap(false);
  canvas.setFont(nullptr);
  canvas.setTextSize(1);

  // Cabecera
  canvas.setTextColor(0xAD55);  // gris claro
  canvas.setCursor(2, 2);
  canvas.print("TOKENS HOY");
  if (state.hasData && strlen(state.date) == 10) {
    char dm[6];  // "DD/MM"
    snprintf(dm, sizeof(dm), "%.2s/%.2s", state.date + 8, state.date + 5);
    canvas.setCursor(canvas.width() - (int16_t)strlen(dm) * 6 - 2, 2);
    canvas.print(dm);
  }
  canvas.drawFastHLine(0, 12, canvas.width(), 0x4208);

  // Clawd a la izquierda
  drawClawd(canvas, 3, 22, currentMood(now), now);

  // Número a la derecha
  const int16_t numX = CLAWD_W + 6;
  const int16_t numW = canvas.width() - numX - 2;
  canvas.setTextColor(ST77XX_WHITE);
  if (state.hasData) {
    drawTokens(state.dayTokens, numX, numW, 52);
  } else {
    canvas.setFont(&FreeSansBold9pt7b);
    const char *l1 = "esperando", *l2 = "PC...";
    canvas.setCursor(numX + (numW - (int16_t)textWidth(l1)) / 2, 40);
    canvas.print(l1);
    canvas.setCursor(numX + (numW - (int16_t)textWidth(l2)) / 2, 58);
    canvas.print(l2);
    canvas.setFont(nullptr);
  }

  // Pie: antigüedad del dato + % del presupuesto
  char foot[32];
  uint16_t footColor = 0x8410;
  if (!state.hasData) {
    snprintf(foot, sizeof(foot), "corre el daemon :)");
  } else {
    unsigned long age = (now - state.lastRxMs) / 1000;
    if (isStale(now)) {
      snprintf(foot, sizeof(foot), "SIN DATOS %lus", age);
      footColor = ST77XX_RED;
    } else {
      snprintf(foot, sizeof(foot), "hace %lus", age);
    }
  }
  canvas.setTextColor(footColor);
  canvas.setCursor(2, 71);
  canvas.print(foot);
  if (state.hasData && state.lvl >= 0) {
    char pct[12];
    snprintf(pct, sizeof(pct), "%d%%", (int)(state.lvl * 100.0f + 0.5f));
    uint16_t c = state.lvl >= LVL_RED ? ST77XX_RED : state.lvl >= LVL_YELLOW ? ST77XX_YELLOW : ST77XX_GREEN;
    canvas.setTextColor(c);
    canvas.setCursor(canvas.width() - (int16_t)strlen(pct) * 6 - 2, 71);
    canvas.print(pct);
  }

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), canvas.width(), canvas.height());
}

// ---------------------------------------------------------------- LED

static uint8_t lerp8(uint8_t a, uint8_t b, float t) { return (uint8_t)(a + (b - a) * t); }

// 0 → verde, LVL_YELLOW → amarillo, LVL_RED → rojo
static void levelColor(float lvl, uint8_t &r, uint8_t &g, uint8_t &b) {
  b = 0;
  if (lvl < 0) lvl = 0;  // sin presupuesto: verde
  if (lvl <= LVL_YELLOW) {
    float t = lvl / LVL_YELLOW;
    r = lerp8(0, 255, t);
    g = lerp8(255, 170, t);
  } else if (lvl < LVL_RED) {
    float t = (lvl - LVL_YELLOW) / (LVL_RED - LVL_YELLOW);
    r = 255;
    g = lerp8(170, 0, t);
  } else {
    r = 255;
    g = 0;
  }
}

// Onda 0..1 con período `periodMs`.
static float wave(uint32_t now, uint32_t periodMs) {
  return 0.5f + 0.5f * sinf((float)(now % periodMs) / periodMs * 2.0f * PI);
}

static void updateLed(uint32_t now) {
  static uint32_t lastMs = 0;
  if (now - lastMs < 30) return;
  lastMs = now;

  if (ledMode == LedMode::Off) {
    led.set(0, 0, 0, 0);
    return;
  }
  uint8_t bri = ledMode == LedMode::Normal ? 6 : 1;  // de 31; el LED es MUY brillante
  uint8_t r, g, b;
  float k;  // intensidad 0..1

  if (!state.hasData) {
    r = 0, g = 60, b = 255;  // azul: esperando al daemon
    k = 0.15f + 0.85f * wave(now, 3000);
  } else if (isStale(now)) {
    r = 160, g = 0, b = 255;  // violeta: el daemon dejó de mandar
    k = 0.1f + 0.9f * wave(now, 5000);
  } else {
    levelColor(state.lvl, r, g, b);
    if (state.lvl >= LVL_RED) {
      k = 0.2f + 0.8f * wave(now, 700);  // rojo latiendo rápido
    } else {
      k = 0.55f + 0.45f * wave(now, 4000);  // respiración suave
    }
    if (now - state.lastActivityMs < 250 && state.everActive) {
      r = g = b = 255;  // flash blanco cuando entran tokens nuevos
      k = 1.0f;
    }
  }
  led.set((uint8_t)(r * k), (uint8_t)(g * k), (uint8_t)(b * k), bri);
}

// Botón BOOT: cicla el LED normal → tenue → apagado.
static void pollButton(uint32_t now) {
  static bool lastPressed = false;
  static uint32_t lastChangeMs = 0;
  bool pressed = digitalRead(PIN_BOOT_BTN) == LOW;
  if (pressed != lastPressed && now - lastChangeMs > 40) {
    lastChangeMs = now;
    lastPressed = pressed;
    if (pressed) {
      ledMode = ledMode == LedMode::Normal ? LedMode::Dim
              : ledMode == LedMode::Dim    ? LedMode::Off
                                           : LedMode::Normal;
    }
  }
}

// ---------------------------------------------------------------- serial

static void handleLine(const char *line) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    Serial.printf("ERR parse: %s\n", err.c_str());
    return;
  }
  JsonVariant tokens = doc["day_tokens"];
  if (!tokens.is<uint64_t>()) {
    Serial.println("ERR falta day_tokens");
    return;
  }
  uint64_t newTokens = tokens.as<uint64_t>();
  const char *date = doc["date"] | "";
  uint32_t now = millis();

  // Subieron los tokens del mismo día => Clawd se pone a laburar.
  if (state.hasData && newTokens > state.dayTokens && strcmp(date, state.date) == 0) {
    state.lastActivityMs = now;
    state.everActive = true;
  }
  state.dayTokens = newTokens;
  state.lvl = doc["lvl"] | -1.0f;
  strlcpy(state.date, date, sizeof(state.date));
  state.hasData = true;
  state.lastRxMs = now;
}

static void pollSerial() {
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') {
      if (!lineOverflow && lineLen > 0) {
        lineBuf[lineLen] = '\0';
        handleLine(lineBuf);
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
}

// ---------------------------------------------------------------- main

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, LOW);  // backlight on (activo en bajo)
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  led.begin();

  lcdSpi.begin(PIN_LCD_SCK, -1, PIN_LCD_MOSI, -1);
  // INITR_MINI160x80_PLUGIN = offsets 26/1, inversión ON y orden BGR:
  // exactamente lo que configura el driver st7735 de LilyGO para este panel.
  tft.initR(INITR_MINI160x80_PLUGIN);
  tft.setSPISpeed(27000000);
  tft.setRotation(TFT_ROTATION);
  tft.fillScreen(ST77XX_BLACK);

  // Autotest del LED: tiene que verse ROJO, VERDE, AZUL en ese orden.
  const uint8_t test[3][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
  for (const auto &c : test) {
    led.set(c[0], c[1], c[2], 6);
    delay(300);
  }
  led.set(0, 0, 0, 0);

  Serial.println("claude-token-monitor listo (clawd + semaforo)");
}

void loop() {
  static uint32_t lastFrameMs = 0;
  uint32_t now = millis();
  pollSerial();
  pollButton(now);
  updateLed(now);
  if (now - lastFrameMs >= FRAME_MS) {
    lastFrameMs = now;
    render(now);
  }
  delay(2);
}
