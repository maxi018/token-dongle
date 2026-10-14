#pragma once
// Clawd en pixel art: el bichito naranja del banner de Claude Code, animado.
//
// La grilla sale del logo ASCII del CLI ( ▐▛███▜▌ / ▝▜█████▛▘ / ▘▘ ▝▝ ),
// pasando cada caracter de bloque a 2x2 "píxeles". Los ojos son los huecos de la fila 1.

#include <Adafruit_GFX.h>

enum class ClawdMood : uint8_t {
  Sleeping,  // sin datos / daemon caído: ojos cerrados y "zZ"
  Idle,      // todo tranqui
  Working,   // los tokens subieron hace poco: camina y rebota
  Sweating,  // cerca del presupuesto: gotita de sudor
  Panic,     // presupuesto pasado: tiembla y suda a mares
};

// Dibuja a Clawd con la esquina superior izquierda en (x, y).
// Ocupa CLAWD_W x CLAWD_H píxeles (más la animación, que se mueve ±3 px).
static const int16_t CLAWD_SX = 3;  // escala horizontal de cada "píxel"
static const int16_t CLAWD_SY = 8;  // escala vertical (los bloques ASCII son altos)
static const int16_t CLAWD_COLS = 18;
static const int16_t CLAWD_ROWS = 5;
static const int16_t CLAWD_W = CLAWD_COLS * CLAWD_SX;  // 54
static const int16_t CLAWD_H = CLAWD_ROWS * CLAWD_SY;  // 40

void drawClawd(Adafruit_GFX &g, int16_t x, int16_t y, ClawdMood mood, uint32_t nowMs);
