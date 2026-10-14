# token-dongle

Monitor de consumo de tokens de Claude Code en un **LILYGO T-Dongle-C5** (ESP32-C5): un daemon
lee `ccusage` y manda los datos por serial a la pantalla TFT, con el LED RGB como semáforo de gasto.

```
┌──────────────── PC (Debian 13) ─────────────────┐            ┌──── T-Dongle-C5 ────┐
│ ~/.claude/*.jsonl ──► ccusage daily --json      │  USB       │ HWCDC (Serial)      │
│                         │                       │  Serial/   │   │ JSON por línea  │
│ claude_token_daemon.py ─┴─► {"day_tokens":…}\n ─┼──JTAG────► │   ▼                 │
│   (systemd --user, cada 10 s)                   │            │ ST7735 80x160 + LED │
└─────────────────────────────────────────────────┘            └─────────────────────┘
```

- **Sin USB-HID**: el ESP32-C5 solo tiene USB Serial/JTAG, así que todo va por serie (CDC).
- **Sin API key**: `ccusage` lee los logs locales de Claude Code.

| Carpeta | Qué hay |
|---|---|
| [`daemon/`](daemon/README.md) | Script Python + servicio systemd + regla udev |
| [`firmware/`](firmware/README.md) | Proyecto PlatformIO para el dongle |

## Arranque rápido

```bash
# 1. Firmware
cd firmware && pio run -t upload --upload-port /dev/ttyACM0

# 2. Daemon
sudo apt install python3-serial && npm i -g ccusage
cd ../daemon && ./claude_token_daemon.py -v
```

## Protocolo PC → dongle

Una línea JSON por mensaje, terminada en `\n`, máximo 512 bytes. El dongle ignora las claves
que no conoce, así que las fases nuevas pueden agregar campos sin romper firmwares viejos.

| Clave | Tipo | Fase | Descripción |
|---|---|---|---|
| `v` | int | 1 | versión del protocolo (`1`) |
| `day_tokens` | uint64 | 1 | total de tokens de hoy (`totalTokens` de `ccusage daily`) |
| `date` | `"YYYY-MM-DD"` | 1 | día al que corresponde |

Si pasan más de 35 s sin mensajes válidos, el dongle muestra **SIN DATOS** en rojo.

## Fuente de datos

Verificado con **ccusage 20.0.31**:

- `ccusage daily --json --since YYYYMMDD --until YYYYMMDD` →
  `{"daily":[{"period":"2026-10-14","totalTokens":…,"totalCost":…,"inputTokens":…,"outputTokens":…,"cacheCreationTokens":…,"cacheReadTokens":…}],"totals":{…}}`
- `ccusage blocks --json --active` (ventana de 5 h, para la fase 2/3) →
  `{"blocks":[{"isActive":true,"startTime":…,"endTime":…,"totalTokens":…,"costUSD":…,"tokenCounts":{…},"burnRate":{"tokensPerMinute":…,"costPerHour":…},"projection":{"totalTokens":…,"totalCost":…,"remainingMinutes":…}}]}`

Ojo: en esta versión `daily` suma **todos** los agentes que detecta (campo `"agent":"all"`), no solo Claude Code.

## Hardware: dos cosas a saber

1. **El LED es un APA102, no un WS2812B.** Tiene clock (GPIO4) + data (GPIO5). Las libs de
   WS2812/NeoPixel no lo van a prender; en la fase 2 se maneja con APA102/SPI.
2. **Los pines no son los del T-Dongle-S3.** Están verificados contra el repo oficial
   [`Xinyuan-LilyGO/T-Dongle-C5`](https://github.com/Xinyuan-LilyGO/T-Dongle-C5); detalle en
   [`firmware/README.md`](firmware/README.md).

## Roadmap

- [x] **Fase 1 (MVP)** — total de tokens del día en texto plano, actualizándose.
- [ ] **Fase 2** — costo USD, burn rate y semáforo con el LED APA102.
- [ ] **Fase 3** — barra de progreso de la ventana de 5 h y mini gráfico histórico.
