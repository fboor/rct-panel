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

`main`, `lcars-panel`, `simulator`: lokal, nie pushen.

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

## Dokumentation

Kurz. Keine Prosa. Tabellen, Listen, Zahlen. Ein Satz pro Aussage. Erklärt wird
nur, was ohne die Erklärung falsch gelesen würde.