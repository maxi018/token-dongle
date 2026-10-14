#!/usr/bin/env python3
"""Claude Token Monitor — daemon de PC (Fase 1).

Cada N segundos corre `ccusage daily --json` para el día de hoy, saca el total
de tokens y lo manda al T-Dongle-C5 por el puerto serie USB (USB Serial/JTAG del
ESP32-C5) como una línea JSON:

    {"v":1,"day_tokens":591725,"date":"2026-10-14"}

Estructura de `ccusage daily --json` verificada con ccusage 20.0.31:

    {"daily": [{"period": "2026-10-14", "totalTokens": 591725, "totalCost": 0.49,
                "inputTokens": ..., "outputTokens": ...,
                "cacheCreationTokens": ..., "cacheReadTokens": ..., ...}],
     "totals": {...}}
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import logging
import shlex
import shutil
import signal
import subprocess
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover - mensaje amigable
    sys.exit("Falta pyserial: `sudo apt install python3-serial` (o `pip install pyserial` en un venv)")

PROTOCOL_VERSION = 1
# USB Serial/JTAG de Espressif (ESP32-C5/C3/C6/S3): VID 0x303A, PID 0x1001
ESPRESSIF_VID = 0x303A
USB_JTAG_SERIAL_PID = 0x1001

log = logging.getLogger("claude-token-daemon")


# --------------------------------------------------------------------- ccusage


def default_ccusage_cmd() -> list[str]:
    """Prefiere un ccusage instalado globalmente (mucho más rápido que npx en cada poll)."""
    if shutil.which("ccusage"):
        return ["ccusage"]
    if shutil.which("bunx"):
        return ["bunx", "ccusage@latest"]
    return ["npx", "-y", "ccusage@latest"]


def run_ccusage(base_cmd: list[str], args: list[str], timeout: float) -> dict:
    cmd = base_cmd + args
    log.debug("ejecutando: %s", shlex.join(cmd))
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, check=False)
    if proc.returncode != 0:
        raise RuntimeError(f"ccusage salió con {proc.returncode}: {proc.stderr.strip()[:300]}")
    # npx puede ensuciar stdout con avisos; el JSON arranca en la primera '{'.
    out = proc.stdout
    start = out.find("{")
    if start < 0:
        raise RuntimeError(f"ccusage no devolvió JSON: {out[:200]!r}")
    return json.loads(out[start:])


def fetch_today(base_cmd: list[str], offline: bool, timeout: float) -> dict:
    today = dt.date.today()
    args = ["daily", "--json", "--since", today.strftime("%Y%m%d"), "--until", today.strftime("%Y%m%d")]
    if offline:
        args.append("--offline")
    data = run_ccusage(base_cmd, args, timeout)

    iso = today.isoformat()
    rows = [r for r in data.get("daily", []) if r.get("period", r.get("date")) == iso]
    day_tokens = sum(int(r.get("totalTokens", 0)) for r in rows)
    return {"v": PROTOCOL_VERSION, "day_tokens": day_tokens, "date": iso}


# ---------------------------------------------------------------------- serial


def find_dongle_port() -> str | None:
    candidates = [p for p in list_ports.comports() if p.vid == ESPRESSIF_VID]
    # Preferimos el USB Serial/JTAG; si no, cualquier cosa de Espressif.
    candidates.sort(key=lambda p: p.pid != USB_JTAG_SERIAL_PID)
    return candidates[0].device if candidates else None


class DongleLink:
    """Conexión serie que se reabre sola si desenchufás el dongle."""

    def __init__(self, port: str | None):
        self.fixed_port = port
        self.ser: serial.Serial | None = None

    def _open(self) -> bool:
        port = self.fixed_port or find_dongle_port()
        if not port:
            log.warning("no encuentro el dongle (VID 303A). ¿Está enchufado?")
            return False
        try:
            # El baudrate se ignora en USB CDC; DTR/RTS quedan como los pone el kernel
            # (igual que un monitor serie), así no disparamos el reset del ESP32.
            self.ser = serial.Serial(port, 115200, timeout=1, write_timeout=2, exclusive=True)
        except (serial.SerialException, OSError) as e:
            log.warning("no pude abrir %s: %s", port, e)
            self.ser = None
            return False
        log.info("conectado a %s", port)
        return True

    def send(self, payload: dict) -> bool:
        if self.ser is None and not self._open():
            return False
        line = json.dumps(payload, separators=(",", ":")) + "\n"
        try:
            self.ser.write(line.encode("ascii"))
            self.ser.flush()
            # Vaciamos lo que mande el dongle (logs/errores) para que no se acumule.
            if self.ser.in_waiting:
                for msg in self.ser.read(self.ser.in_waiting).decode(errors="replace").splitlines():
                    if msg.strip():
                        log.debug("dongle> %s", msg.strip())
        except (serial.SerialException, OSError) as e:
            log.warning("se cortó el serial: %s", e)
            self.close()
            return False
        return True

    def close(self) -> None:
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None


# ------------------------------------------------------------------------ main


def main() -> int:
    ap = argparse.ArgumentParser(description="Manda el consumo de tokens de Claude Code al T-Dongle-C5.")
    ap.add_argument("--port", help="puerto serie (default: autodetecta VID 303A, ej. /dev/ttyACM0)")
    ap.add_argument("--interval", type=float, default=10.0, help="segundos entre polls (default 10)")
    ap.add_argument("--ccusage-cmd", help='comando de ccusage (default: "ccusage" si está, si no "npx -y ccusage@latest")')
    ap.add_argument("--offline", action="store_true", help="pasa --offline a ccusage (precios cacheados, sin red)")
    ap.add_argument("--timeout", type=float, default=90.0, help="timeout de cada corrida de ccusage")
    ap.add_argument("--dry-run", action="store_true", help="no abre el serial, solo imprime el payload")
    ap.add_argument("--once", action="store_true", help="una sola vuelta y sale")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )

    base_cmd = shlex.split(args.ccusage_cmd) if args.ccusage_cmd else default_ccusage_cmd()
    log.info("usando ccusage: %s", shlex.join(base_cmd))

    link = None if args.dry_run else DongleLink(args.port)
    running = True

    def stop(*_):
        nonlocal running
        running = False

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)

    while running:
        t0 = time.monotonic()
        payload = None
        try:
            payload = fetch_today(base_cmd, args.offline, args.timeout)
            log.info("hoy: %s tokens", f"{payload['day_tokens']:,}".replace(",", "."))
        except (RuntimeError, subprocess.TimeoutExpired, json.JSONDecodeError, OSError) as e:
            # No mandamos nada: el dongle mantiene el último número y su
            # "SIN DATOS hace Xs" deja ver que algo anda mal.
            log.error("falló ccusage: %s", e)

        if payload is not None:
            if link is None:
                print(json.dumps(payload, separators=(",", ":")), flush=True)
            else:
                link.send(payload)

        if args.once:
            break
        # Dormimos en pedacitos para que SIGTERM corte rápido.
        while running and time.monotonic() - t0 < args.interval:
            time.sleep(0.2)

    if link is not None:
        link.close()
    log.info("chau")
    return 0


if __name__ == "__main__":
    sys.exit(main())
