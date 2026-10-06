# Anweisungen für Agenten

Arbeitsregeln für dieses Repository. Sie sind **keine Projektdokumentation** —
`README.md` und `docs/` sind für Menschen, die das Gerät benutzen, und dort steht
nichts von diesem Abschnitt.

## 1. Nichts, was Maschine oder Entwickler identifiziert, in Commit-Nachrichten

| Nie in einer Nachricht | Begründung |
|---|---|
| MAC-Adressen | des Panels, des Wechselrichtlers, jeder Netzwerkschnittstelle |
| lokale Pfade | das eigene Home-Verzeichnis, Benutzerverzeichnisse, Scratch-Verzeichnisse |
| Personennamen | wer den Test gefahren hat, wem die Karte gehört, wer den Fehler gemeldet hat |

Eine Nachricht ist der schlechteste Ort dafür: Sie gehört nicht zum Baum, ein
späterer Edit nimmt sie also nicht wieder heraus, und ein umgeschriebener Commit
behält den alten Text in der Historie.

**Schreibe den Befund, nicht den Namen der Maschine dafür.** Statt
„Verified on the panel: ESP32-S3 rev 0.2, MAC …" also „Verified on the panel:
ESP32-S3 rev 0.2, the new build boots". In Testdaten eine synthetische Adresse,
die keinem Board gehören kann — mit Buchstaben geschrieben, damit die Prüfung
unten nicht die Prüfung selbst findet.

Ein Befehl findet sie, und ihn vor dem Commit zu laufen lassen ist billiger als
eine neue Nachricht hinterher:

```sh
git log --format=%B --all \
  | grep -inE '\b([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\b|/home/|/Users/|/tmp/'
```

## 2. Kein Rewrite der Historie

Ein Commit, der existiert, bleibt wie er ist. Kein `git commit --amend`, kein
`git rebase`, kein `git filter-branch`, kein `--force` — nicht für eine MAC und
nicht für einen Tippfehler.

Nicht die Größe der Änderung ist der Grund, sondern die Kopien: Ein Rewrite
ändert die SHA unter allen späteren Commits, und der Commit passt dann nicht mehr
zu dem, was andere Clones, das Reflog und der Hosting-Dienst bereits haben.

Steht schon etwas Identifizierendes in einem existierenden Commit, ist die
Antwort ein **neuer** Commit — oder, wenn es nur im Nachrichtentext steht, gar
keiner — und die Fundstelle wird notiert (siehe Punkt 3).

## 3. Bekannte Fundstellen

Nicht mehr beheben, nur nicht wieder suchen. Jede neue Zeile kommt hier hin, wenn
ein Fund befunden statt behoben wurde.

| Wo | Was | Warum sie bleibt |
|---|---|---|
| `tools/json_scan_test/test_json_scan.cpp` | eine Wechselrichter-MAC als JSON-Testdatum; belegt, dass eine MAC mit Doppelpunkten gelesen wird | bereits auf `origin/master` |
| `d869113` | ein Scratch-Verzeichnis in der Commit-Nachricht | bereits auf `origin/master` |

## 4. Sprachen

- Commit-Nachrichten **englisch**, im Ton des Projekts: was kaputt war, warum es
  so aussah, und was das Messen ergeben hat.
- `docs/` und das Handbuch **deutsch**, weil sie gelesen werden. Dateinamen in
  `docs/` bleiben unverändert.