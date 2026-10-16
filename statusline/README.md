# Status line: actualiza el dongle sin daemon

Claude Code ejecuta la *status line* después de cada respuesta. `dongle_statusline.py`
aprovecha eso para:

1. **Disparar el envío al dongle** (`daemon/claude_token_daemon.py --push`) en segundo plano:
   corre ccusage y manda los datos por serial. No queda ningún proceso corriendo.
2. **Guardar tus límites de 5 h y semanal** para las barras.

Ráfagas de respuestas no saturan nada: como mucho un envío cada 10 s, y siempre se manda
uno al final con los datos más nuevos. La status line en sí tarda ~50 ms (no espera a ccusage).
El log del envío queda en `~/.cache/token-dongle/push.log`.

## Límites de 5 h y semanal

Claude Code le pasa a su *status line* los **porcentajes oficiales** de los límites de tu
cuenta (los mismos que ves con `/usage`), en el campo `rate_limits`
([docs](https://code.claude.com/docs/en/statusline#rate-limit-usage)):

```json
"rate_limits": {
  "five_hour": {"used_percentage": 23.5, "resets_at": 1738425600},
  "seven_day": {"used_percentage": 41.2, "resets_at": 1738857600}
}
```

`dongle_statusline.py` guarda eso en `~/.cache/token-dongle/limits.json` y el envío lo
manda al dongle para dibujar las barras **5h** y **sem**.

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

Opciones (se agregan al comando en `~/.claude/settings.json`, o con `install.py --no-push`):

| Opción | Qué hace |
|---|---|
| `--push-args "..."` | opciones para el envío, ej. `"--offline --budget-usd 20"` o `"--offline --port /dev/t-dongle-c5"` |
| `--no-push` | solo guarda los límites; usalo si preferís el [daemon](../daemon/README.md) |

Para sacarla y volver a la tuya:

```bash
python3 ~/token-dongle/statusline/install.py --uninstall
```

## Limitaciones (importante)

- **Barras solo con planes Pro/Max.** Con API key no existen estos límites: las barras no
  aparecen, pero el número, Clawd y el LED (con presupuesto en USD) andan igual.
- **Se actualiza solo mientras usás Claude Code en la terminal de esta PC.** Si usás Claude Code desde
  VS Code / JetBrains y el dongle no se actualiza, es porque ahí no corre la status line: usá el modo daemon. Si después
  usás Claude en la web o en el celular, ese consumo **sí** cuenta para tu límite, pero el
  dongle no se entera hasta que vuelvas a usar Claude Code acá. Cuando los datos tienen más de
  1 hora, las barras se ponen grises para avisarte que pueden estar viejos.
- Cuando una ventana pasa su `resets_at`, el dongle la muestra en 0% solo (lleva la cuenta
  regresiva), porque arrancó una nueva.
