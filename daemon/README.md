# Daemon de PC

Script Python que cada 10 s corre `ccusage`, saca el total de tokens del día y lo manda al
dongle por el puerto serie USB. Pensado para Debian 13, anda en cualquier Linux.

## Requisitos

```bash
sudo apt install python3 python3-serial nodejs npm
sudo usermod -aG dialout "$USER"     # permiso al puerto serie (cerrá sesión y volvé a entrar)

# Recomendado: ccusage instalado, así no se resuelve npx en cada poll
npm install -g ccusage               # o: npm config set prefix ~/.npm-global primero, sin sudo
```

Si no está `ccusage` en el PATH, el daemon cae a `bunx ccusage@latest` o `npx -y ccusage@latest`
(funciona, pero tarda más en cada vuelta).

### Regla udev (recomendada)

Evita que ModemManager le mande comandos AT al dongle, da acceso sin `dialout` y crea `/dev/t-dongle-c5`:

```bash
sudo cp 99-t-dongle-c5.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
```

## Correrlo a mano

```bash
./claude_token_daemon.py --dry-run --once   # prueba ccusage sin tocar el serial
./claude_token_daemon.py -v                 # autodetecta el dongle (VID 303A) y manda cada 10 s
./claude_token_daemon.py --port /dev/t-dongle-c5 --interval 15
```

| Flag | Default | Qué hace |
|---|---|---|
| `--port` | autodetecta | puerto serie del dongle |
| `--interval` | `10` | segundos entre polls |
| `--ccusage-cmd` | `ccusage` / `bunx` / `npx` | comando a ejecutar, ej. `"npx -y ccusage@20"` |
| `--offline` | off | `ccusage --offline`: precios cacheados, sin pegarle a internet |
| `--timeout` | `90` | timeout de cada corrida de ccusage |
| `--budget-usd` | `50` | presupuesto diario en USD para el semáforo (`0` = no usar) |
| `--budget-tokens` | `0` | presupuesto diario en tokens (`0` = no usar). Si usás los dos, manda el más exigido |
| `--dry-run` | off | imprime el payload en vez de mandarlo |
| `--once` | off | una vuelta y sale |
| `-v` | off | logs de debug (incluye lo que responde el dongle) |

Si desenchufás el dongle, el daemon sigue corriendo y se reconecta solo cuando vuelve.

## Como servicio systemd (de usuario)

Va como servicio **de usuario** porque `ccusage` tiene que leer tu `~/.claude`.

```bash
# Ajustá ExecStart/PATH en el .service si el repo no está en ~/token-dongle
mkdir -p ~/.config/systemd/user
cp claude-token-monitor.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now claude-token-monitor

journalctl --user -u claude-token-monitor -f     # logs
systemctl --user stop claude-token-monitor       # antes de flashear el dongle
```

Para que arranque aunque no hayas iniciado sesión gráfica: `sudo loginctl enable-linger "$USER"`.

**PATH:** systemd no lee tu `.bashrc`. Si `ccusage`/`node` vienen de nvm, fnm, bun, etc., agregá su
carpeta en la línea `Environment=PATH=...` o pasá la ruta absoluta con `--ccusage-cmd`.

## Qué manda

Una línea JSON por actualización (ver el README general para el protocolo):

```json
{"v":1,"day_tokens":1640234,"date":"2026-10-14","lvl":0.156}
```

`lvl` = costo de hoy / `--budget-usd` (y/o tokens / `--budget-tokens`). Con 0.6 el LED ya está
amarillo y con 1.0 rojo. El costo es el **equivalente en API** que calcula ccusage: si tenés plan
Pro/Max no es lo que pagás, pero sirve igual como termómetro.

`day_tokens` es el `totalTokens` de la fila de hoy de `ccusage daily --json`
(input + output + cache creation + cache read).
