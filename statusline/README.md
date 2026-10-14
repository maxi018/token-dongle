# Status line: límites de 5 h y semanal

Claude Code le pasa a su *status line* los **porcentajes oficiales** de los límites de tu
cuenta (los mismos que ves con `/usage`), en el campo `rate_limits`
([docs](https://code.claude.com/docs/en/statusline#rate-limit-usage)):

```json
"rate_limits": {
  "five_hour": {"used_percentage": 23.5, "resets_at": 1738425600},
  "seven_day": {"used_percentage": 41.2, "resets_at": 1738857600}
}
```

`dongle_statusline.py` es una status line que guarda eso en `~/.cache/token-dongle/limits.json`.
El daemon lo lee y lo manda al dongle para dibujar las barras **5h** y **sem**.

## Instalar

```bash
python3 ~/token-dongle/statusline/install.py
```

- Hace backup de `~/.claude/settings.json` (`settings.json.bak-FECHA`).
- **Si ya tenías una status line, la conserva:** la envuelve con `--wrap` y la seguís viendo igual.
  La original queda guardada en `~/.config/token-dongle/previous_statusline.json`.
- Si no tenías, te muestra una mínima: `🦀 [Opus] · 5h 23% · sem 41%`.

Después abrí Claude Code (o reinicialo) y mandá cualquier mensaje: los límites recién llegan
**después de la primera respuesta** de la sesión.

Para sacarla y volver a la tuya:

```bash
python3 ~/token-dongle/statusline/install.py --uninstall
```

## Limitaciones (importante)

- **Solo planes Pro/Max.** Con API key no existen estos límites y las barras no aparecen.
- **Se actualiza solo mientras usás Claude Code en la terminal de esta PC.** Si después
  usás Claude en la web o en el celular, ese consumo **sí** cuenta para tu límite, pero el
  dongle no se entera hasta que vuelvas a usar Claude Code acá. Cuando los datos tienen más de
  30 minutos, las barras se ponen grises para avisarte que pueden estar viejos.
- Cuando una ventana pasa su `resets_at`, el daemon la muestra en 0%, porque arrancó una nueva.
