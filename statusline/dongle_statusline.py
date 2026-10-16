#!/usr/bin/env python3
"""Status line de Claude Code que le pasa los límites de uso al token-dongle.

Claude Code le da a su status line un JSON por stdin; para suscriptores Pro/Max trae:

    "rate_limits": {
      "five_hour": {"used_percentage": 23.5, "resets_at": 1738425600},
      "seven_day": {"used_percentage": 41.2, "resets_at": 1738857600}
    }

(https://code.claude.com/docs/en/statusline#rate-limit-usage). Son los porcentajes
oficiales de tu cuenta, los mismos que muestra /usage. Este script:

1. Guarda esas ventanas en ~/.cache/token-dongle/limits.json.
2. Dispara en segundo plano `daemon/claude_token_daemon.py --push`, que corre ccusage y le
   manda todo al dongle. Así no hace falta dejar un daemon corriendo: el dongle se actualiza
   cada vez que Claude responde. (Desactivable con --no-push si preferís el daemon.)
3. Imprime una status line. Si ya tenías una, pasala con --wrap y se sigue viendo la tuya.

Nunca debe romper la status line: cualquier error se traga en silencio.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
import tempfile
import time

DEFAULT_FILE = os.path.join(
    os.environ.get("XDG_CACHE_HOME", os.path.expanduser("~/.cache")), "token-dongle", "limits.json"
)
WINDOWS = ("five_hour", "seven_day")
PUSH_SCRIPT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "daemon", "claude_token_daemon.py")
)
PUSH_LOG = os.path.join(os.path.dirname(DEFAULT_FILE), "push.log")


def spawn_push(extra_args: str) -> None:
    """Lanza el envío al dongle desacoplado: la status line no espera a ccusage."""
    os.makedirs(os.path.dirname(PUSH_LOG), exist_ok=True)
    try:
        if os.path.getsize(PUSH_LOG) > 256 * 1024:
            os.truncate(PUSH_LOG, 0)
    except OSError:
        pass
    with open(PUSH_LOG, "a") as logf:
        subprocess.Popen(
            [sys.executable, PUSH_SCRIPT, "--push", *shlex.split(extra_args)],
            stdin=subprocess.DEVNULL,
            stdout=logf,
            stderr=logf,
            start_new_session=True,  # sobrevive aunque Claude Code corte la status line
            close_fds=True,
        )


def save_limits(data: dict, path: str) -> dict:
    """Mezcla las ventanas nuevas con las guardadas y escribe el archivo de forma atómica."""
    try:
        with open(path) as f:
            saved = json.load(f)
    except (OSError, ValueError):
        saved = {}

    rl = data.get("rate_limits") or {}
    changed = False
    for name in WINDOWS:
        w = rl.get(name)
        if isinstance(w, dict) and "used_percentage" in w:
            saved[name] = {
                "used_percentage": float(w["used_percentage"]),
                "resets_at": int(w.get("resets_at") or 0),
            }
            changed = True
    # Si no vino ninguna ventana (inicio de sesión, o ya reseteó) dejamos lo que había:
    # el daemon sabe que una ventana con resets_at vencido vale 0%.
    if changed:
        saved["updated_at"] = int(time.time())
        os.makedirs(os.path.dirname(path), exist_ok=True)
        fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix=".limits.")
        with os.fdopen(fd, "w") as f:
            json.dump(saved, f)
        os.replace(tmp, path)
    return saved


def default_line(data: dict, limits: dict) -> str:
    now = time.time()
    parts = []
    model = (data.get("model") or {}).get("display_name")
    if model:
        parts.append(f"[{model}]")
    for name, label in (("five_hour", "5h"), ("seven_day", "sem")):
        w = limits.get(name)
        if w:
            pct = 0.0 if w.get("resets_at", 0) and w["resets_at"] <= now else w["used_percentage"]
            parts.append(f"{label} {pct:.0f}%")
    return "🦀 " + " · ".join(parts) if parts else "🦀"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", default=DEFAULT_FILE, help="dónde guardar los límites")
    ap.add_argument("--wrap", help="tu status line anterior: se ejecuta con el mismo JSON y se muestra su salida")
    ap.add_argument("--no-push", action="store_true", help="no mandar al dongle (si usás el daemon)")
    ap.add_argument("--push-args", default="--offline",
                    help='opciones para el envío, ej. "--offline --budget-usd 20" (default: --offline)')
    args = ap.parse_args()

    raw = sys.stdin.read()
    data: dict = {}
    limits: dict = {}
    try:
        data = json.loads(raw)
        limits = save_limits(data, args.file)
    except Exception:
        pass
    if not args.no_push and os.path.exists(PUSH_SCRIPT):
        try:
            spawn_push(args.push_args)
        except Exception:
            pass

    if args.wrap:
        try:
            out = subprocess.run(args.wrap, shell=True, input=raw, capture_output=True, text=True, timeout=5)
            sys.stdout.write(out.stdout)
        except Exception:
            pass
    else:
        print(default_line(data, limits))
    return 0


if __name__ == "__main__":
    sys.exit(main())
