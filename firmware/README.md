# Firmware — LILYGO T-Dongle-C5

PlatformIO + framework Arduino (arduino-esp32 **3.3.12**, sobre ESP-IDF 5.5).

## Hardware (verificado en el repo oficial de LilyGO)

Fuente: [`Xinyuan-LilyGO/T-Dongle-C5`](https://github.com/Xinyuan-LilyGO/T-Dongle-C5):
`include/pin_config.h`, la tabla de pines del `readme.md`, `examples/LCD` y `board/Lilygo-T-Dongle-C5.json`.

| Función | GPIO | Notas |
|---|---|---|
| LCD MOSI | 2 | ST7735 0.96" 80x160, SPI |
| LCD SCK | 6 | |
| LCD CS | 10 | |
| LCD DC (RS) | 3 | |
| LCD RST | 1 | |
| LCD backlight | 0 | **activo en bajo** (`LOW` = prendido) |
| LED CI (clock) | 4 | **APA102**, no WS2812B |
| LED DI (data) | 5 | |
| Botón BOOT | 28 | |
| USB D-/D+ | 13 / 14 | USB Serial/JTAG |

El panel usa offsets 26/1, inversión de color ON y orden BGR (así lo configura el driver
`lib/lcd_st7735` de LilyGO). En el firmware usamos la lib de Adafruit con
`INITR_MINI160x80_PLUGIN`, que pone exactamente esa misma configuración
(rotación 1 de Adafruit = `MADCTL MY|MV|BGR` = rotación 3 de LilyGO).

## Soporte del ESP32-C5

- El C5 entró en **arduino-esp32 3.3.0** y la última estable es **3.3.12** (hay una 4.0 en RC).
- La plataforma oficial `platformio/espressif32` se quedó en arduino-esp32 2.x y **no** soporta el C5.
  Por eso usamos **pioarduino** (la que usa LilyGO), pero fijada a la release `55.03.312-1`
  en vez de la rama `develop` como hace el ejemplo de LilyGO.
- Board: `boards/lilygo-t-dongle-c5.json`, copiada tal cual del repo de LilyGO
  (16 MB flash QIO, 8 MB PSRAM, partición `default_16MB.csv`).
- `ARDUINO_USB_MODE=1` + `ARDUINO_USB_CDC_ON_BOOT=1` → `Serial` va por el USB Serial/JTAG.

## Compilar y flashear

Necesitás PlatformIO Core (o la extensión de VS Code):

```bash
python3 -m venv ~/.venvs/pio && ~/.venvs/pio/bin/pip install platformio
export PATH="$HOME/.venvs/pio/bin:$PATH"

cd firmware
pio run                                      # compila (la 1ra vez baja el toolchain, tarda un rato)
pio run -t upload --upload-port /dev/ttyACM0 # flashea
pio device monitor -p /dev/ttyACM0           # opcional: ver logs
```

Si el upload no engancha (por ejemplo, porque el firmware de fábrica tiene el USB ocupado):

1. Desenchufá el dongle.
2. Mantené apretado el botón **BOOT** y enchufalo.
3. Soltá BOOT y repetí `pio run -t upload`.
4. Desenchufá y volvé a enchufar (o tocá reset) para que arranque el firmware nuevo.

Para encontrar el puerto: `ls -l /dev/serial/by-id/` → buscá `usb-Espressif_USB_JTAG_serial_debug_unit_*`.
Si instalaste la regla udev del daemon, también tenés `/dev/t-dongle-c5`.

> **Ojo:** el monitor serie y el daemon no pueden tener el puerto abierto a la vez.
> Pará el servicio (`systemctl --user stop claude-token-monitor`) antes de flashear o monitorear.

### ¿Arduino IDE?

También anda: core **esp32 by Espressif ≥ 3.3.0**, placa *ESP32C5 Dev Module*,
`USB CDC On Boot: Enabled`, `Flash Size: 16MB`, `PSRAM: Enabled`,
`Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)`. Instalá *Adafruit GFX*,
*Adafruit ST7735 and ST7789* y *ArduinoJson*, y renombrá `src/main.cpp` a `.ino`.
PlatformIO es lo recomendado porque deja todo pineado.

## Prueba manual sin daemon

Con el monitor serie abierto (`pio device monitor`), pegá esta línea y Enter:

```json
{"v":1,"day_tokens":1234567,"date":"2026-10-14"}
```

O desde bash:

```bash
stty -F /dev/ttyACM0 raw -echo
echo '{"v":1,"day_tokens":1234567,"date":"2026-10-14"}' > /dev/ttyACM0
```

## Pantalla (Fase 1)

```
┌────────────────────────────────┐
│TOKENS HOY                 14/10│
│────────────────────────────────│
│          1.640.234             │  ← la fuente se achica sola si no entra
│                                │
│     actualizado hace 4s        │  ← en rojo "SIN DATOS hace Xs" si pasan >35s
└────────────────────────────────┘
```

Si la imagen te queda dada vuelta según cómo enchufes el dongle, cambiá `TFT_ROTATION` a `3` en `src/main.cpp`.
