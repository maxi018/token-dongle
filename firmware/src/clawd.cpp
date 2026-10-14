#include "clawd.h"

static const char *const GRID[CLAWD_ROWS] = {
    "...############...",
    "...##.######.##...",  // ojos: huecos en las columnas 5 y 12
    ".################.",  // brazitos a los costados
    "...############...",
    "....#.#....#.#....",  // patitas
};
static const int16_t EYE_COLS[2] = {5, 12};
static const int16_t LEG_ROW = 4;

static const uint16_t BODY = 0xDBAA;   // #D97757, el naranja terracota de Claude
static const uint16_t SWEAT = 0x5D7F;  // celeste
static const uint16_t ZZZ = 0x8C71;    // gris

static void cell(Adafruit_GFX &g, int16_t x, int16_t y, int16_t col, int16_t row, uint16_t c,
                 int16_t hFrom = 0, int16_t hTo = CLAWD_SY) {
  g.fillRect(x + col * CLAWD_SX, y + row * CLAWD_SY + hFrom, CLAWD_SX, hTo - hFrom, c);
}

static void drawDrop(Adafruit_GFX &g, int16_t x, int16_t y) {
  g.fillTriangle(x + 2, y, x, y + 3, x + 4, y + 3, SWEAT);
  g.fillCircle(x + 2, y + 4, 2, SWEAT);
}

void drawClawd(Adafruit_GFX &g, int16_t x, int16_t y, ClawdMood mood, uint32_t now) {
  // --- desplazamientos de la animación
  int16_t dx = 0, dy = 0;
  bool stepA = (now / 220) % 2 == 0;
  switch (mood) {
    case ClawdMood::Working:
      dy = stepA ? -3 : 0;  // salta mientras camina
      break;
    case ClawdMood::Idle:
    case ClawdMood::Sweating:
      dy = ((now / 900) % 2) ? -1 : 0;  // respira
      break;
    case ClawdMood::Panic:
      dx = ((now / 70) % 3) - 1;  // tiembla
      break;
    case ClawdMood::Sleeping:
      dy = 1;
      break;
  }
  x += dx;
  y += dy;

  // --- cuerpo
  for (int16_t r = 0; r < CLAWD_ROWS; r++) {
    for (int16_t c = 0; c < CLAWD_COLS; c++) {
      if (GRID[r][c] != '#') continue;
      if (r == LEG_ROW && mood == ClawdMood::Working) {
        // Pasito: un par de patas apoyado, el otro levantado (más corto).
        bool firstPair = (c == 4 || c == 11);
        if (firstPair != stepA) {
          cell(g, x, y, c, r, BODY, 0, CLAWD_SY / 2);
          continue;
        }
      }
      cell(g, x, y, c, r, BODY);
    }
  }

  // --- ojos (los huecos quedan del color de fondo)
  bool blink = (now % 3700) < 140;
  bool closed = mood == ClawdMood::Sleeping || blink;
  if (closed) {
    for (int16_t ec : EYE_COLS) cell(g, x, y, ec, 1, BODY, 0, CLAWD_SY - 2);
  }

  // --- extras según el humor
  if (mood == ClawdMood::Sweating || mood == ClawdMood::Panic) {
    int16_t fall = (now / 120) % 8;
    drawDrop(g, x + 15 * CLAWD_SX + 2, y - 4 + fall);
    if (mood == ClawdMood::Panic) {
      int16_t fall2 = (now / 90 + 4) % 8;
      drawDrop(g, x + 1 * CLAWD_SX - 6, y - 2 + fall2);
    }
  }
  if (mood == ClawdMood::Sleeping) {
    uint8_t t = (now / 600) % 3;
    g.setFont(nullptr);
    g.setTextColor(ZZZ);
    g.setTextSize(1);
    g.setCursor(x + CLAWD_W - 6, y + 4 - t * 2);
    g.print("z");
    if (t > 0) {
      g.setCursor(x + CLAWD_W, y - 4 - t * 2);
      g.print("Z");
    }
  }
}
