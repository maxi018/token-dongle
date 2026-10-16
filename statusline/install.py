#!/usr/bin/env python3
"""Instala dongle_statusline.py como status line de Claude Code.

- Hace backup de ~/.claude/settings.json antes de tocarlo.
- Si ya tenías una status line, la conserva: la envuelve con --wrap, así seguís viéndola.
- Correrlo dos veces no hace nada raro.
- `--uninstall` deja la status line que tenías antes.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import sys
import time

SCRIPT = os.path.abspath(os.path.join(os.path.dirname(__file__), "dongle_statusline.py"))
MARK = "dongle_statusline.py"
# La status line que tenías antes se guarda acá (no en settings.json) para poder volver.
PREVIOUS_FILE = os.path.join(
    os.environ.get("XDG_CONFIG_HOME", os.path.expanduser("~/.config")), "token-dongle", "previous_statusline.json"
)


def load_previous() -> dict | None:
    try:
        with open(PREVIOUS_FILE) as f:
            return json.load(f) or None
    except (OSError, ValueError):
        return None


def save_previous(previous: dict | None) -> None:
    if previous is None:
        if os.path.exists(PREVIOUS_FILE):
            os.remove(PREVIOUS_FILE)
        return
    os.makedirs(os.path.dirname(PREVIOUS_FILE), exist_ok=True)
    with open(PREVIOUS_FILE, "w") as f:
        json.dump(previous, f, indent=2)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--settings", default=os.path.expanduser("~/.claude/settings.json"))
    ap.add_argument("--uninstall", action="store_true")
    ap.add_argument("--no-push", action="store_true",
                    help="no actualizar el dongle desde la status line (si usás el daemon)")
    args = ap.parse_args()

    settings: dict = {}
    if os.path.exists(args.settings):
        with open(args.settings) as f:
            settings = json.load(f)
        backup = f"{args.settings}.bak-{time.strftime('%Y%m%d-%H%M%S')}"
        shutil.copy2(args.settings, backup)
        print(f"backup: {backup}")

    current = settings.get("statusLine") or {}
    current_cmd = current.get("command", "") if current.get("type") == "command" else ""
    ours = MARK in current_cmd

    if args.uninstall:
        if not ours:
            print("la status line del dongle no está instalada; no toco nada")
            return 0
        previous = load_previous()
        if previous:
            settings["statusLine"] = previous
        else:
            settings.pop("statusLine", None)
        save_previous(None)
        print("status line del dongle desinstalada")
    else:
        if ours:
            print("ya estaba instalada; actualizo la ruta por las dudas")
            previous = load_previous()
        else:
            previous = current or None
            save_previous(previous)
        cmd = f"python3 {shlex.quote(SCRIPT)}"
        if args.no_push:
            cmd += " --no-push"
        prev_cmd = (previous or {}).get("command") if (previous or {}).get("type") == "command" else None
        if prev_cmd:
            cmd += f" --wrap {shlex.quote(prev_cmd)}"
            print(f"tu status line anterior se sigue viendo: {prev_cmd}")
        new = {"type": "command", "command": cmd}
        if previous and "padding" in previous:
            new["padding"] = previous["padding"]
        settings["statusLine"] = new
        print("status line del dongle instalada")

    os.makedirs(os.path.dirname(args.settings), exist_ok=True)
    tmp = args.settings + ".tmp"
    with open(tmp, "w") as f:
        json.dump(settings, f, indent=2, ensure_ascii=False)
        f.write("\n")
    os.replace(tmp, args.settings)
    print("listo: abrí (o reiniciá) Claude Code y mandá un mensaje para que se actualice")
    return 0


if __name__ == "__main__":
    sys.exit(main())
