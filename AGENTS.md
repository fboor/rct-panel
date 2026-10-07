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

Kein Rewrite: kein `amend`, kein `rebase`, kein `filter-branch`, kein
`--force`. Auch nicht für einen Tippfehler — ein Rewrite ändert die SHA unter
allen späteren Commits.

Fund in einem existierenden Commit: neuer Commit, oder keiner wenn es nur im
Nachrichtentext steht. Notieren statt beheben.

## Bekannte Fundstellen

| Wo | Was | Warum |
|---|---|---|
| `tools/json_scan_test/test_json_scan.cpp` | Wechselrichter-MAC als JSON-Testdatum | auf `origin/master` |
| `d869113` | Scratch-Pfad in der Nachricht | auf `origin/master` |
| `aa0ef58` | dasselbe | auf `origin/master` |

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