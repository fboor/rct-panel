# tools/

Entwicklerwerkzeuge. Kein Board, keine Karte und kein Wechselrichter nötig, sofern nicht anders angegeben.

| Aufruf | Zweck |
|---|---|
| `tools/run_host_tests.sh` | alle Host-Tests: 19 C++/JS-Gruppen und, falls daneben liegend, `rct_sim_test`. |
| `tools/<name>_test/run.sh` | eine einzelne Testgruppe; Zweck steht im Header von `run_host_tests.sh`. |
| `tools/panel_sim/run.sh` | Firmware-Oberfläche auf dem Rechner, SDL2-Fenster und Web-UI auf `:8081`; `--shot` schreibt ein PNG. Eigene `README.md`. |
| `tools/pack_web_install.sh` | baut die vier Images (zwei Boards × de/en) nach `docs/install/v<Version>/`, schreibt Manifeste, setzt `devices.json`. |
| `tools/flash_usb.sh` | `firmware.bin` per USB; löscht vorher `otadata`, sonst bootet ein Web-Update-Slot weiter. |
| `tools/git_sha.py` | SCons-Hook, setzt `RCT_BUILD_SHA` aus HEAD; `-dirty` bei Quelländerungen, `docs/` zählt nicht. |
| `tools/watch_boot.py` | liest Serialausgabe nur, ohne Auto-Reset: `python3 tools/watch_boot.py /dev/ttyUSB0 115200 <s>`. |
| `tools/size_report.py` | Flash/RAM/PSRAM aus der Linker-Map, eigene Anteile getrennt vom Framework. |
| `tools/md2pdf{,-de,-en}.py` | rendert `docs/benutzerhandbuch*.md` mit python-markdown und chromium zu PDF. |
| `tools/img2ascii.py` | Bild als ASCII im Terminal, `--edge` für Kanten. |

Testgruppen: `backlight`, `badge`, `board`, `chart_color`, `crc`, `device`, `flow_layout`, `i18n`, `json_scan`, `json`, `jstest`, `numfmt`, `oig_field`, `rctevent`, `rctframe`, `relay`, `ring_json`, `sd_queue`, `theme`.
