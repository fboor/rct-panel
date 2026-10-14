# Anweisungen für Agenten

Keine Projektdokumentation. `README.md` und `docs/` sind für Anwender.

Die Firmware löscht nie `/hist` und keine Monatsdatei. Die einzigen zwei
Löschungen im ganzen Code sind `/hist/PROBE` aus dem Selbsttest und ein
Screenshot, der nicht vollständig geschrieben wurde (in `/shot`).

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
| **Der CH422G hat keine einstellbare I²C-Adresse — die Registernummer IST die Adresse** (`ESP_IO_EXPANDER_I2C_CH422G_ADDRESS` ist 0x24, steht aber mit dem Kommentar „but it is ignored"). Unser Treiber schrieb das Register als erstes Datenbyte an eine feste Adresse; deshalb fand der Bus-Scan nur den GT911 und es hieß „kein IO-Expander" auf einem Board, das einen hat. **Gemessen am 14.10.2026 nach der Korrektur am 7-Zoll-Panel: `CH422G: 12 Ausgaenge auf GPIO 8/9`**, und mit funktionierendem EXIO4 auch `SD: mounted at 4000000 Hz, self-test 17.5 kB/s, 4.0 GB free`. Dieselbe Adresse steht in ESPHome und in der Registerliste der LoVanGFX-Anfrage zu genau diesem Display — das ist die zweite Implementierung, die man hier fragen muss | `src/board/Ch422G.cpp` |
| **Karte auf dem 7-Zoll-Board**: ihr Chip Select ist EXIO4 am Expander, also kein GPIO, und der Arduino-SD-Treiber fährt den CS-Pin selbst (`pinMode(cs)`, `digitalWrite(cs, LOW)` um jede Übertragung) — er bekommt dort deshalb den Dummy GPIO 15, so wie es das Hersteller-Demo macht (`wsd.h`: `SD_SS = -1`, `SD_CS = 4`). Mit laufendem Expander **gemessen am 14.10.2026**: `SD: mounted at 4000000 Hz, self-test 17.5 kB/s, 4.0 GB free`. Ohne funktionierenden Expander bliebe EXIO4 hoch und die Karte nicht auswählbar; der IDF-Weg (`gpio_cs = -1`) würde sie auch nicht erreichen, sondern nur die ganze Datei ein zweites Mal über `FILE*` verlangen | `src/storage/sdlog.cpp` |
| Reiter auf „Energie" sind je 184 px breit bei 53 px Text und 34 px hoch gegen einen 16 px Balken — das Aussehen ist noch offen, die Geometrie kommt jetzt aus dem Profil | `src/ui/UiLayout.cpp` |
| „Service" auf dem 7-Zoll-Panel nutzt die Breite jetzt (Spalten bei 20 und 580), lässt aber 120 px in der Mitte frei und führt die rechte Spalte mit 40 px Abständen bis y=352 | `src/gui/GuiApp.cpp` |
| Das Springen heißt bei Espressif *drift*, und die beiden genannten Gegenmaßnahmen liegen beide in ESP-IDF: Octal-PSRAM mit 120 MHz und XIP aus dem PSRAM (die Board-Datei holt 80 MHz), und der Bounce-Puffer, den der gelinkte `esp_lcd` nicht hat. Bei 80 MHz sind laut FAQ ~22 MHz Pixel-Takt erlaubt, wir laufen mit 16 | `docs/hardware.md` |
| Ein Upgrade auf `espressif32@7.x` bringt **nichts** für den Drift: gemessen am 14.10.2026 baut die Firmware damit ohne eine einzige Fehlermeldung, aber der gelinkte `esp_lcd` kommt weiter aus `tools/sdk` des Cores und hat weder Bounce-Puffer noch `num_fbs`. Aufwand des Messens: eine Stunde | `platformio.ini` |
| **Immer `tools/flash_usb.sh` benutzen, nie ein nacktes `write_flash 0x10000`.** Das Skript löscht vorher `otadata`, damit der Bootloader auf `app0` zurückfällt. Ohne das schreibt ein Flash nach 0x10000 in einen Slot, den das Panel nicht bootet — es bootet dann `app1` weiter, weil ein Web-Update diesen Slot gewählt hat. Sieht aus wie „Flash erfolgreich, Bild unverändert" | `tools/flash_usb.sh` |
| PSRAM läuft **oktal mit 80 MHz** (am 14.10.2026 gemessen: 7 MB von 8 MB benutzbar; ein Quad-Modus könnte den Chip nicht lesen). Damit gilt die FAQ-Grenze von ~22 MHz Pixel-Takt — wir fahren 16 MHz. Das Springen kam also nicht aus dem PSRAM-Modus | `src/display/Display.cpp` |
| Der **Blaustich des 7-Zoll-Panels** ist kein Fehler des Datenwegs: gemessen am 14.10.2026 über `/api/px.json` geht der Seitengrund als `FFFF` raus und die Balken als `3A2A` = `0x3A4550`; `0xFFFF` überlebt jede Bitpermutation, jede Drehung und jede Kanalsortierung, ein Versandfehler kann Weiß also nicht einfärben. **Er war aber zu einem Teil unsere Abweichung**: mit den vertikalen Porches 16/16 (unsere Gegenmaßnahme gegen das Springen) war der Stich deutlich stärker, mit den 8/8 des Herstellers ist er deutlich besser (14.10.2026, Augenschein) — und das Springen ist bis auf Netzwerktraffic weg. Übrig bleibt die Übergangsfunktion des Moduls; ein Stromaus setzt den ST7262 zurück und ändert daran nichts. Ausgleichen ließe es sich nur mit einer Verstärkung je Kanal: `corrBuild(rD, bD, gD, gH)` kann absenken, nicht heben | `src/display/Display.cpp`, `src/gui/GuiApp.cpp` |
| Rechte Navigationstaste auf dem 7-Zoll-Board: sie geht, seit der Touch-Bereich nicht mehr auf 480 px feststeht — dort stand die halbe Bildschirmbreite als tote Zone | `src/display/Touch.cpp` |
| Seitenwechsel dauern spürbar (20 Streifen, jeder mit Kopie und Cache-Rückschreiben über den ganzen Framebuffer). Der Bildwechsel geht vor dem Zeichnen im alten Zustand, also sichtbar von oben nach unten | `src/display/Display.cpp` |
| Der RCT-Abruf blockiert die Hauptschleife, weil das **Gerät** auf manche OIDs nicht antwortet und der Treiber die volle Ruhepause wartet (`RCT_QUIET_MS` 300 ms). Gemessen am 14.10.2026 am 7-Zoll-Panel: 470 ms, und 2947 ms / 3266 ms in den Zyklen, in denen das Gerät gar nicht antwortete. Die Oberfläche friert dabei sichtbar ein — Werte, Uhr und Navigation stehen still | `src/rct/RctDriver.cpp` |
| Release-Images tragen kein `RCT_SIM_HOST` (am 14.10.2026 im Artefakt geprüft, nicht im Build-Log). Das Gerät selbst wird über die Oberfläche eingestellt und steht deshalb in keinem Image | `platformio.ini`, `tools/pack_web_install.sh` |

## Release

Ein Release ist veröffentlicht, wenn das Binary im Ordner `docs/install/v<Version>/`
liegt, der Ordner committed ist und der Tag gesetzt ist. Ab dann bleibt es: **ein
Release wird nicht neu gebaut.** Was danach im Baum passiert, gehört in die nächste
Version — `RCT_FW_VERSION` wird erst beim nächsten Release erhöht, nicht rückwirkend.

Vor dem Tag darf derselbe Ordner beliebig oft neu gebaut werden (das war die
Arbeitsfassung von 1.0-rc3, zweimal neu gebaut). Nach dem Tag sind `docs/install/
v1.0-rc3/*` Bytes, die jemand aus dem Browser installiert hat, und ein Panel, das
darüber gebaut wurde.

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

Zwei Befunde, die beide eine Messung über den einen Vorgang hinaus verlangten:

| Befund | Was gemessen gehört | Warum |
|---|---|---|
| Eine Zeile stand in der Quelle und fehlte im Image — toter Code nach `return`, vom Compiler ohne Warnung geworfen | Das **Artefakt** (`firmware.bin`), nicht die Build-Ausgabe | Ein grüner Build ist kein Beweis für eine Änderung |
| Jeder Download nach dem ersten lieferte HTTP 200 mit **0 Byte**: `done` stand vom letzten Download noch drin, weil die Karte in einer **anderen Task** zurückgesetzt wird und der Web-Layer sofort liest. Behoben durch `sdBeginStreamRequest()` vor der Anfrage | **Vier Downloads** derselben Datei (1 152 054 B, alle vier bytegleich) | Ein einzelner Request prüft einen Zustand, den der vorige Request geschrieben hat |

## Dokumentation

Kurz. Keine Prosa. Tabellen, Listen, Zahlen. Ein Satz pro Aussage. Erklärt wird
nur, was ohne die Erklärung falsch gelesen würde.