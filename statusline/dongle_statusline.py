#!/usr/bin/env python3
"""Status line de Claude Code que le pasa los límites de uso al token-dongle.

Claude Code le da a su status line un JSON por stdin; para suscriptores Pro/Max trae:

    "rate_limits": {
      "five_hour": {"used_percentage": 23.5, "resets_at": 1738425600},
      "seven_day": {"used_percentage": 41.2, "resets_at": 1738857600}
    }

(https://code.claude.com/docs/en/statusline#rate-limit-usage). Son los porcentajes
oficiales de tu cuenta, los mismos que muestra /usage. Este script:

1. Guarda esas ventanas en ~/.cache/token-dongle/limits.json (lo lee el daemon).
2. Imprime una status line. Si ya tenías una, pasala con --wrap y se sigue viendo la tuya.

Nunca debe romper la status line: cualquier error se traga en silencio.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time

DEFAULT_FILE = os.path.join(
    os.environ.get("XDG_CACHE_HOME", os.path.expanduser("~/.cache")), "token-dongle", "limits.json"
)
WINDOWS = ("five_hour", "seven_day")


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
    args = ap.parse_args()

    raw = sys.stdin.read()
    data: dict = {}
    limits: dict = {}
    try:
        data = json.loads(raw)
        limits = save_limits(data, args.file)
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
