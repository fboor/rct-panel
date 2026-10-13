# Anweisungen für Agenten

Keine Projektdokumentation. `README.md` und `docs/` sind für Anwender.

## Commit-Nachrichten

Nie: MAC-Adressen, lokale Pfade, Personennamen. Den Befund schreiben, nicht den
Namen der Maschine dafür.

Vor dem Commit:

```sh
git log --format=%B --all | grep -inE '\b([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\b|/home/|/Users/|/tmp/'
```

Muster breit (`/tmp/`, nicht ein Scratch-Name). Ein engeres hat zwei
Fundstellen übersehen.

## Push

Nur `master`. Kein Push ohne Freigabe.

`main`, `lcars-panel`, `simulator`, `portal-design`: lokal, nie pushen.
`portal-design` ist der Arbeitsbranch für das Portal-Design; getestet wird über den
Emulator, nicht über das Panel.

## Historie

Kein Rewrite, sobald ein Commit gepusht ist: kein `amend`, kein `rebase`, kein
`filter-branch`, kein `--force`. Auch nicht für einen Tippfehler — ein Rewrite
ändert die SHA unter allen späteren Commits.

**Gepushte Commits sind tabu. Ungepushte dürfen vor dem Push umsortiert und
zusammengefasst werden**, solange der letzte gepushte Commit (`origin/master`) die
Grenze bleibt und am Ende derselbe Dateistand dasteht. Grund: 38 Commits für eine
Arbeitswoche, davon mehr als die Hälfte verworfene Zwischenschritte (ein Takt, der
sich als Ursache ausgegeben hat und keiner war; eine Puffergröße, die um die Hälfte
zu groß war; eine Logumleitung auf einen Port, den das Board gar nicht hat), sind
als Historie schlechter zu lesen als als eine einzige Zeile pro Ergebnis.

Fund in einem existierenden **gepushten** Commit: neuer Commit, oder keiner wenn es
nur im Nachrichtentext steht. Notieren statt beheben.

## Bekannte Fundstellen

| Wo | Was | Warum |
|---|---|---|
| `tools/json_scan_test/test_json_scan.cpp` | Wechselrichter-MAC als JSON-Testdatum | auf `origin/master` |
| `d869113` | Scratch-Pfad in der Nachricht | auf `origin/master` |
| `aa0ef58` | dasselbe | auf `origin/master` |

## Offene Punkte

| Was | Wo |
|---|---|
| Build-Kennung ist ein Compiler-Flag: nach einem Commit, der andere Datei ändert, übersetzt `pio run` die Objektdateien nicht neu und das Image trägt die alte | `tools/git_sha.py`, `tools/pack_web_install.sh` |
| Release-Binaries neu bauen, `devices.json` prüfen | `docs/install/v1.0-rc2/` |
| Das gelinkte `libesp_lcd.a` kann weder Bounce-Puffer noch zwei Framebuffer. Der Flush-Callback wartet nur noch darauf, dass der Strahl **nicht auf dem Streifen** ist (`on_frame_trans_done` + Zeiten aus dem Boardprofil); das frühere Warten auf „Strahl ist vorbei" kostete bis zu ein halbes Frame je Streifen. Der neuere Treiber liegt im `-libs`-Paket, wird aber nicht gelinkt — Struct und Bibliothek müssen dieselbe Fassung sein. Das Springen kam nicht vom Schreibzeitpunkt, sondern vom PSRAM-Schreibdruck (LVGL-Puffer jetzt im SRAM) | `src/display/Display.cpp` |
| Auf dem 7-Zoll-Board antwortet **kein** IO-Expander: ein Bus-Scan über 0x08..0x77 findet nur den GT911 bei 0x14. Verdrahtung und Adresse stimmen mit der Hersteller-Konfiguration (SDA 8, SCL 9, 0x20). Folge: LCD-Reset, USB/CAN-Umschalter, Karten-Chip-Select und Touch-Reset laufen nie. **Nach jedem Flash muss das Panel vom Strom genommen werden**, sonst bleibt der Bildschirm weiß (die Firmware zeichnet, der Scan läuft mit 29,4 fps) | `src/board/Ch422G.cpp` |
| Karte auf dem 7-Zoll-Board: Chip Select ist EXIO4, kein GPIO; die Arduino-SD-Bibliothek verträgt das nicht, der Mount muss über den IDF-Treiber mit `gpio_cs = -1` | `src/storage/sdlog.cpp` |
| Rechte Navigationstaste auf dem 7-Zoll-Board: sie geht, seit der Touch-Bereich nicht mehr auf 480 px feststeht — dort stand die halbe Bildschirmbreite als tote Zone | `src/display/Touch.cpp` |
| Seitenwechsel dauern spürbar (20 Streifen, jeder mit Kopie und Cache-Rückschreiben über den ganzen Framebuffer). Der Bildwechsel geht vor dem Zeichnen im alten Zustand, also sichtbar von oben nach unten | `src/display/Display.cpp` |
| Der RCT-Abruf blockiert die Hauptschleife ~470 ms alle 10 s: der Treiber wartet die volle Ruhepause (`RCT_QUIET_MS` 300 ms), weil der Simulator auf manche OIDs nicht antwortet. Die Oberfläche friert dabei sichtbar ein — Werte, Uhr und Navigation stehen 0,5 s still | `src/rct/RctDriver.cpp` |
| Das 7-Zoll-Board ist seit dem 13.10. mit einem Image aus `RCT_SIM_HOST` unterwegs; das steht nicht im Repo. Für ein Release: ohne das Flag bauen | `platformio.ini` |

## Sprache

- Commit-Nachrichten: englisch
- `docs/`, Handbuch: deutsch, weil sie gelesen werden. Dateinamen unverändert.
- `AGENTS.md`: deutsch, wie `docs/`.

## Fremde Angaben gegenprüfen

Werte aus einer Doku gelten als **Vermutung**, bis sie gegen das Werkzeug geprüft
sind, das zum Chip gehört. Die Doku ist am vertrauenswürdigsten und am wenigsten
geprüft.

| Angabe | Gegengeprüft an |
|---|---|
| Bootloader-Adresse beim Flashen | dem Builder der Platform (`espressif32/builder/`), der dieselbe Adresse benutzt |
| Flash-Parameter | dem, was `flash_id` am Gerät meldet |
| Register einer Chip-Erweiterung | einer zweiten Implementierung (ESPHome fährt denselben Chip) |
| Pinbelegung | der Konfigurationsdatei, die der Hersteller selbst kompiliert |

Belegte Fälle:

- ESP Web Tools nennt `4096` für den Bootloader — das ist der **ESP32**. Der S3
  nimmt `0x0`, sonst bootet ein Panel nicht, das vorher lief.
- Drei Stellen, an denen ein fremdes Beispiel, eine Fremdquelle oder eine
  Annahme das Bild verstellt haben, sind in `3388eaa`, `6457f02` und `9fe4481`
  beschrieben.

## Reihenfolge beim Fehlersuchen

Die einfache Erklärung zuerst. Zwei Flash-Vorgänge mit demselben Ergebnis
(`invalid header 0xFFFFFFFF`) wurden zuerst mit den Flash-Parametern erklärt; die
Bytes waren gleich, nur die Adresse nicht. Und der Vorschlag „das musst du am
falschen Gerät probieren" war falsch, weil eine Installation vorher löscht — jedes
Ziel ist danach ein leeres Gerät.

## Dokumentation

Kurz. Keine Prosa. Tabellen, Listen, Zahlen. Ein Satz pro Aussage. Erklärt wird
nur, was ohne die Erklärung falsch gelesen würde.