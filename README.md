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

![Pantalla simulada en sus distintos estados](docs/preview.png)

*Render de la pantalla (160x80, ampliada) con la misma librería gráfica del firmware.*

| Carpeta | Qué hay |
|---|---|
| [`daemon/`](daemon/README.md) | Script Python + servicio systemd + regla udev |
| [`firmware/`](firmware/README.md) | Proyecto PlatformIO para el dongle |
| [`statusline/`](statusline/README.md) | Status line de Claude Code que pasa los límites 5h/semana (opcional) |

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
| `lvl` | float | 2 | qué tan cerca del límite estás: maneja LED y humor de Clawd (`0.6` amarillo, `1.0` rojo). Opcional |
| `s5`, `s7` | int | 2 | % usado de la ventana de 5 h y de la semanal (límites oficiales Pro/Max). Opcional |
| `r5` | int | 2 | segundos hasta que resetea la ventana de 5 h. Opcional |
| `lim_age` | int | 2 | antigüedad de los límites en segundos (más de 30 min = barras grises). Opcional |

Si pasan más de 35 s sin mensajes válidos, el dongle muestra **SIN DATOS** en rojo.

## Fuente de datos

Verificado con **ccusage 20.0.31**:

- `ccusage daily --json --since YYYYMMDD --until YYYYMMDD` →
  `{"daily":[{"period":"2026-10-14","totalTokens":…,"totalCost":…,"inputTokens":…,"outputTokens":…,"cacheCreationTokens":…,"cacheReadTokens":…}],"totals":{…}}`
- `ccusage blocks --json --active` (ventana de 5 h, para la fase 2/3) →
  `{"blocks":[{"isActive":true,"startTime":…,"endTime":…,"totalTokens":…,"costUSD":…,"tokenCounts":{…},"burnRate":{"tokensPerMinute":…,"costPerHour":…},"projection":{"totalTokens":…,"totalCost":…,"remainingMinutes":…}}]}`

Ojo: en esta versión `daily` suma **todos** los agentes que detecta (campo `"agent":"all"`), no solo Claude Code.

## ¿Qué cuenta y qué no?

Hay dos fuentes distintas, y cuentan cosas distintas:

| | **Número grande** (ccusage) | **Barras 5h / sem** (status line) |
|---|---|---|
| Qué es | tokens que quedaron en los logs locales (`~/.claude`) de **esta PC** | el % oficial de tus límites, el mismo que `/usage` |
| Claude Code en la terminal de esta PC | ✅ | ✅ |
| Extensiones de VS Code / JetBrains en esta PC | ✅ (usan el mismo Claude Code) | ✅ cuentan; la barra se refresca seguro solo desde la terminal |
| claude.ai web, app de escritorio, celular | ❌ | ✅ cuentan para el límite, pero la barra se entera cuando vuelvas a usar la terminal |
| Claude Code en la web o en otra PC | ❌ (los logs quedan allá) | ✅ ídem |
| Otros agentes que ccusage detecta (ej. Codex) | ✅ los suma también | ❌ |

En resumen: el número es "cuánto laburó Claude Code en esta máquina" y las barras son "cuánto
te queda de verdad", porque los límites son compartidos entre todos los lugares donde usás Claude.

## Clawd y el semáforo

| Estado | Clawd | LED |
|---|---|---|
| Sin datos todavía | duerme (zZ) | azul respirando |
| Lejos del límite | tranqui, respira | verde → amarillo |
| Entraron tokens nuevos | camina y salta ~12 s | flash blanco |
| Cerca del límite | suda una gotita | amarillo → rojo |
| Límite al 90% o más (o presupuesto pasado) | tiembla y suda a mares | rojo latiendo rápido |
| Daemon sin mandar > 35 s | duerme, pie "SIN DATOS" en rojo | violeta respirando |

El semáforo sigue **el peor de tus límites reales** (5h o semanal): amarillo desde ~54% y rojo
desde 90%. Si no instalaste la status line (o no tenés plan Pro/Max), usa el presupuesto del
daemon (`--budget-usd`, default 50; o `--budget-tokens`). Con `--led-source budget` forzás el presupuesto.
El botón **BOOT** del dongle cicla el LED: normal → tenue → apagado.

## Hardware: dos cosas a saber

1. **El LED es un APA102, no un WS2812B.** Tiene clock (GPIO4) + data (GPIO5). Las libs de
   WS2812/NeoPixel no lo van a prender; en la fase 2 se maneja con APA102/SPI.
2. **Los pines no son los del T-Dongle-S3.** Están verificados contra el repo oficial
   [`Xinyuan-LilyGO/T-Dongle-C5`](https://github.com/Xinyuan-LilyGO/T-Dongle-C5); detalle en
   [`firmware/README.md`](firmware/README.md).

## Roadmap

- [x] **Fase 1 (MVP)** — total de tokens del día en texto plano, actualizándose.
- [x] **Clawd** animado según el consumo + **semáforo** con el LED APA102.
- [x] **Barras de límites** 5h y semanal (oficiales, vía status line) + cuenta regresiva del reset.
- [ ] **Fase 2** — costo USD y burn rate en pantalla.
- [ ] **Fase 3** — barra de progreso de la ventana de 5 h y mini gráfico histórico.
