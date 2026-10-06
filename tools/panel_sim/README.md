# panel_sim — das Panel in einem Fenster

Die Firmware-Oberfläche, auf dem Rechner, in einem SDL-Fenster. Kein Layout
hier: `src/gui/GuiApp.cpp` wird unverändert übersetzt, dazu LVGL 9.3.0 aus dem
PlatformIO-Verzeichnis. Was im Fenster steht, steht auch auf dem Panel.

## Aufrufen

```sh
tools/panel_sim/run.sh                        # Fenster, deutsch, 480x480
tools/panel_sim/run.sh --lang en              # englischer Build
tools/panel_sim/run.sh --page 4               # auf Seite 4 klicken
tools/panel_sim/run.sh --shot p4.png --speed 20
tools/panel_sim/run.sh --data fang.json       # andere Werte
tools/panel_sim/run.sh --size 800x480         # zweite Aufloesung
```

| Argument | Bedeutung |
|---|---|
| `--shot DATEI` | ein Bild schreiben und beenden (PNG) |
| `--page 1..7` | auf die Seite klicken, wie ein Finger |
| `--data DATEI` | JSON mit den Werten |
| `--size BxH` | Fenstergrösse |
| `--lang en` | englischer Build (Compile-Flag) |
| `--speed N` | Paneluhr N-fach (nur für `--shot`) |
| `--after s` | Sekunden der Paneluhr vor dem Bild |

Voraussetzungen: SDL2, zlib, ein C/C++-Compiler, LVGL 9.3.0 unter
`.pio/libdeps/esp32-s3/lvgl` (sonst `LVGL_DIR` setzen).

## Was gefakt ist

Nur was die Oberfläche von aussen braucht: die Uhr, `Serial`, die SD-Karte
(„keine Karte"), das Netz („verbunden"), die Einstellungen (im Speicher), den
Schaltausgang („aus") und die Gerätedaten aus der JSON-Datei.

| Datei | Inhalt |
|---|---|
| `sim_main.cpp` | Fenster, LVGL-Takt, Seitenklicks, PNG-Ausgabe |
| `sim_stubs.cpp` | Uhr, Serial, SD, Netz, Relais, Web, Diagnose |
| `sim_data.cpp` | JSON → `DeviceState` |
| `stubs/` | `Arduino.h`, `Preferences.h`, `WiFi.h`, ESP-Teile |
| `data/rct_mock.json` | ein Mittag im Juli: alle vier Werte, einer negativ |

## Grenzen

- **Kein Diagramm.** `sdTakeHistory()` gibt −1 zurück, also nimmt das 24-h-Blatt
  den Zweig wie eine Karte ohne Historie.
- **Keine Farbkorrektur.** `dispCorrectPixel()` gibt die Pixel unverändert
  zurück; das Panel korrigiert pro Kanal. Die Farben im Fenster sind also die
  Sollfarben, nicht die des Geräts.
- **Die 480er-Literale stecken noch in `GuiApp.cpp`.** `--size 800x480` zeigt
  deshalb ein 480 breites Layout in einem 800 breiten Fenster. Genau das ist
  Stufe 5/6 des Hardware-Plans, und diesen Weg hier sichtbar zu machen ist der
  Punkt.