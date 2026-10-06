# panel_sim — das Panel in einem Fenster

Die Firmware-Oberfläche auf dem Rechner. Kein Layout hier: `src/gui/GuiApp.cpp`
unverändert, LVGL 9.3.0 aus `.pio/libdeps/esp32-s3/lvgl`.

## Bauen und starten

```sh
tools/panel_sim/run.sh                                   # Fenster, de, 480x480
tools/panel_sim/run.sh --lang en                         # englischer Build
tools/panel_sim/run.sh --page 4                          # auf Seite 4 klicken
tools/panel_sim/run.sh --shot p4.png --speed 20          # Bild, dann Ende
tools/panel_sim/run.sh --data fang.json --size 800x480
```

Voraussetzungen: SDL2, zlib, C/C++-Compiler, LVGL unter `.pio/libdeps/esp32-s3/lvgl`
(sonst `LVGL_DIR`).

| Argument | Bedeutung |
|---|---|
| `--shot DATEI` | PNG schreiben, dann beenden |
| `--page 1..7` | Seite per Klick ansteuern |
| `--data DATEI` | JSON mit den Werten |
| `--size BxH` | Fenstergrösse |
| `--lang en` | englischer Build |
| `--speed N` | Paneluhr N-fach |
| `--after s` | Panelsekunden vor dem Bild |

Erster Lauf: 419 LVGL-Dateien, danach Sekunden. `build/` ist nicht im Baum.

## Was gefakt ist

| Ding | Antwort |
|---|---|
| Uhr | Echtzeit, `millis()` eine Quelle wie im Panel |
| Werte | `data/rct_mock.json` → `DeviceState` |
| SD-Karte | keine; `sdTakeHistory()` −1 |
| Netz | verbunden, kein Provisioning |
| Einstellungen | im Speicher, nicht in NVS |
| Schaltausgang | aus |
| Farbkorrektur | keine, `dispCorrectPixel()` unverändert |
| Wartungscode | wird beim Start gedruckt |

## Grenzen

- **Kein 24-h-Diagramm** — Zweig wie eine Karte ohne Historie
- **Keine zweite Auflösung** — die 480-Literale stecken noch in `GuiApp.cpp`
  (Stufe 5/6 des Hardware-Plans); `--size 800x480` zeigt ein 480er-Layout
- **Seitenwechsel per Klick** — echte SDL-Mausereignisse, Knopfmitten bei
  x = 80 / 240 / 390, y = 442, gemessen am Bild

## Dateien

| Datei | Inhalt |
|---|---|
| `sim_main.cpp` | Fenster, LVGL-Takt, Klicks, PNG |
| `sim_stubs.cpp` | Uhr, Serial, SD, Netz, Relais, Web |
| `sim_data.cpp` | JSON → `DeviceState` |
| `stubs/` | `Arduino.h`, `Preferences.h`, `WiFi.h`, ESP-Teile |
| `data/rct_mock.json` | Mittag im Juli, alle vier Werte, einer negativ |