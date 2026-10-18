# panel_sim — das Panel in einem Fenster

Die Firmware-Oberfläche auf dem Rechner. Kein Layout hier: `src/gui/GuiApp.cpp`
unverändert, LVGL 9.3.0 aus `.pio/libdeps/esp32-s3/lvgl`.

## Bauen und starten

```sh
tools/panel_sim/run.sh                                   # Fenster, de, 480x480
tools/panel_sim/run.sh --lang en                         # englischer Build
tools/panel_sim/run.sh --page 4                          # auf Seite 4 klicken
tools/panel_sim/run.sh --shot p4.png --speed 20          # Bild, dann Ende
tools/panel_sim/run.sh --size 800x480                    # zweites Boardprofil
tools/panel_sim/run.sh --device RCT --host 192.168.1.83  # echter Wechselrichter
```

Weboberfläche: **http://localhost:8081/**, solange das Fenster läuft.
Wartungscode steht beim Start in der Konsole.

## Umschalten im Browser

`/einstellungen` → **Typ**. Drei Einträge: Simulation, RCT Power, OpenInverterGateway.
Umschalten wirkt sofort — der Treiber wird beendet und mit den neuen Einstellungen
neu begonnen, wie beim Neustart eines Panels.

| Von → Auf | Antwort |
|---|---|
| SIM → RCT, echte Adresse | 200, danach echte Daten des Wechselrichters |
| RCT → SIM | 200, danach die emulierte Tageskurve |
| beliebig → OIG ohne erreichbares Gateway | 200, danach „Haus nein, Akku nein, Netz nein" |
| RCT ohne Adresse | 400, es wird nichts gespeichert |
| unbekannter Typ | 400 |

| Startzeile | Bedeutung |
|---|---|
| `--device SIM` | Vorgabe |
| `--device RCT --host … --port …` | echter Wechselrichter über TCP |
| `--device OIG --host …` | OpenInverterGateway |

Voraussetzungen: SDL2, zlib, C/C++-Compiler, LVGL unter `.pio/libdeps/esp32-s3/lvgl`
(sonst `LVGL_DIR`).

| Argument | Bedeutung |
|---|---|
| `--shot DATEI` | PNG schreiben, dann beenden |
| `--page 1..7` | Seite per Klick ansteuern |
| `--data DATEI` | JSON mit den Werten für den SIM-Treiber |
| `--device SIM` | Vorgabe: emulierter Wechselrichter |
| `--device RCT` | echter Wechselrichter über TCP, mit `--host`/`--port` |
| `--host`, `--port` | Adresse des echten Geräts |
| `--size BxH` | Fenstergrösse |
| `--lang en` | englischer Build |
| `--speed N` | Paneluhr N-fach |
| `--after s` | Panelsekunden vor dem Bild |

Erster Lauf: 419 LVGL-Dateien, danach Sekunden. `build/` ist nicht im Baum.

## Einstellungen merken

`data/simulator.json` — **nicht im Repository** (`.gitignore`), weil dort eine
Geräteadresse steht und die nicht in die Historie gehört.

| Ebene | Gewinnt |
|---|---|
| Kommandozeile | `--device`, `--host`, `--port`, `--data`, `--size` |
| `data/simulator.json` | das zuletzt Gespeicherte |
| Vorgabe | **Simulation**, `data/rct_mock.json`, 480x480 |

Ohne die Datei startet der Simulator also **emuliert**, nie gegen ein fremdes Gerät.
Ein Entwickler, der immer mit einem echten Wechselrichter testet, stellt ihn einmal
im Browser ein — und der nächste Start kommt so hoch.

## Was die Anlage im Treiber verliert

`simVerlustW` in der Mock-Datei ist der Verlust der Anlage, und er geht **vor** der
Entscheidung des Akkus ab: er sitzt zwischen den Quellen und dem Haus. Vorgabe ist 0,
also verhält sich der Tagesgang ohne ihn wie bisher.

Der Wert steht nicht aus Bequemlichkeit dort. Das Modell hielt die Bilanz
zwangsläufig geschlossen — was der Akku nicht deckte, ging über den Zähler — und konnte
damit den Zustand gar nicht erzeugen, in dem die Quellen mehr hergeben, als das Haus
nimmt. Genau diese Annahme steckt in der Regel, die einen Bogen Akku → Netz für eine
Einspeisung zeichnete, die kein Zähler gemessen hatte: `simVerlustW` macht den Fall
überhaupt erst darstellbar, `data/mock_verlust_akku_ueber_haus.json` zeigt ihn.

Datei löschen = zurück auf der Vorgabe.

## Neustart

Wie auf dem Panel: ein Gerätewechsel startet den Prozess neu (`execv`), **die
Kommandozeilenargumente zum Gerät fallen dabei weg** — sonst gewinnt wieder die
Kommandozeile und die Umschaltung wirkt nicht.

| Auslöser | Kommandozeile | Ergebnis |
|---|---|---|
| Gerät gewechselt | `--device --host --port` **entfallen** | gespeicherte Datei entscheidet |
| „Neustart" | bleibt | so gestartet, wie getippt |
| nur Theme geändert | bleibt | wirkt sofort, ohne Neustart |

Bremse: nach drei Neustarts in Folge läuft der Simulator ohne weiteren weiter und
sagt es. Ein Programm, das sich selbst neu startet, kann das endlos tun.

## Was gefakt ist

| Ding | Antwort |
|---|---|
| Uhr | Echtzeit, `millis()` eine Quelle wie im Panel |
| Werte | SIM-Treiber: Tageskurve aus der Paneluhr, Zahlen aus der Datei |
| Geraeteschicht | **echt** — `Device.cpp`, `DeviceFactory.cpp`, `Rules.h` |
| Treiber | SIM in `sim_device.cpp`; RCT und OIG die ausgelieferten |
| Weboberflaeche | **echt** — `src/web/WebServer.cpp` auf Sockets |
| SD-Karte | keine; Streams melden Fehler, die Seiten kommen |
| Einstellungen | im Speicher; `saveConfig()` sagt, dass es nicht speichert |
| Schaltausgang | aus |
| Farbkorrektur | keine, `dispCorrectPixel()` unverändert |
| Update, mDNS, ESP.restart | geben sich als gescheitert zu |

## Grenzen

- **Kein 24-h-Diagramm** — Zweig wie eine Karte ohne Historie
- **800x480 ist ein Entwurf, kein fertiges Layout** — das Profil steht in
  `src/ui/UiLayout.cpp` und ist von Hand gesetzt. Zwei Fehler sind sichtbar:
  „Eigenverbrauchsquote" läuft aus der 186 px breiten Karte, und die Karten sind
  mit 150 px Höhe zu hoch für ihren Inhalt. Beides ist Designarbeit, kein Codefehler
- **Keine unbekannte Grösse** — `--size 123x456` sagt „kein Boardprofil" und
  zeichnet nicht auf 480 runter
- **Seitenwechsel per Klick** — echte SDL-Mausereignisse; die Knopfmitten kommen
  aus dem Board (Sechstel der Breite, Mitte der Leiste)

## Dateien

| Datei | Inhalt |
|---|---|
| `sim_main.cpp` | Fenster, LVGL-Takt, Klicks, PNG |
| `sim_stubs.cpp` | Uhr, Serial, SD, Netz, Relais, Web |
| `sim_data.cpp` | öffnet die Datei und reicht sie dem Treiber |
| `sim_device.cpp` | der SIM-Treiber: Tageskurve, Bilanz, Ladestand |
| `sim_board.cpp` | `uiLayout()` des Simulators |
| `stubs/` | `Arduino.h`, `WiFi.h` mit `WiFiClient`, `WebServer.h`, NVS, Update, mDNS |
| `sim_board.cpp` | `uiLayout()` des Simulators: das gewünschte Profil statt der Firmware |
| `data/rct_mock.json` | Mittag im Juli, alle vier Werte, einer negativ |

## Die Zustände zur Einspeisung

Festwerte (`"fixed": 1`), mit denen sich die Frage prüfen lässt, was der Ring zwischen
Akku und Netz zeigt. Jeder hat seinen Testfall in `tools/device_test`, und die
Bilddateien sind nicht im Repository — `run.sh --shot` schreibt sie, `--data` liest den
Zustand:

```sh
run.sh --shot a.png --data data/mock_verlust_akku_ueber_haus.json
```

**Die Regel:** Einspeisung wird zuerst und bis zur Erzeugung von den Panels gedeckt — sie
können nichts lagern, der Akku schon. Der Akku ist der letzte Baustein, und im
Normalfall speist er gar nicht: nur eine Sonderlage wie die Kalibrierung lässt das zu, und
die Firmware liest diesen Zustand noch nicht (OID `0x70A2AF4F` liegt im Treiber und wird
nicht ausgewertet). Deshalb ist der Bogen unten rechts in **allen** vier Zuständen grau.

| Datei | Die vier Werte (PV / Haus / Akku / Zähler) | Was das Bild zeigen muss |
|---|---|---|
| `mock_einspeisung_gegemessen.json` | 300 / 400 / +200 / −100 | **Oberer Ring rot** (die 100 W sind Panels, sie erzeugen dreimal so viel), Speiche Akku → Haus rot, **Bogen unten rechts grau** |
| `mock_einspeisung_ueber_pv.json` | 200 / 500 / +900 / −1100 | **Oberer Ring rot** (die Panels decken 200 der 1100 W), Akku versorgt das Haus, **Bogen grau** — die Einspeisung ist höher als die Erzeugung, und der Akku speist trotzdem nicht |
| `mock_verlust_akku_ueber_haus.json` | 0 / 500 / +700 / 0 | **Bogen grau.** Der Akku gibt mehr her, als das Haus nimmt, und der Zähler misst nichts: die 200 W Differenz sind Verlust, kein Weg ins Netz |
| `mock_verlust_beim_export.json` | 0 / −400 / +700 / −420 | **Bogen grau**, Strebe Haus → Netz rot bei −420 W. Der Export des Haushalts ist die Messung des Hauszählers und bleibt, was er sagte |

Drei dieser vier Anlagen gehen nicht auf, und das ist der Punkt: die vier Messungen
kommen nicht gleichzeitig herein, ein verlustbehaftetes System verliert einige Prozent,
und was der Akku abgibt, ohne dass es eine Quelle im Bild bekommt, bleibt eine Lücke.
Wer in einer dieser Dateien eine Zahl ändert, um die Bilanz zu retten, baut eine Anlage,
die es nicht gibt. Siehe `ruleFlowSplit()` in `src/device/Rules.h`.