# Den Wechselrichter austauschbar machen

## Worum es geht

Das Panel spricht heute über genau ein Protokoll mit genau einem Gerät: dem
RCT Power, 60 Register über TCP auf Port 8899, ein gemeinsamer Bus, CRC16,
Byte-Escaping. Das ist in `src/rct/` auch schon gebündelt, was die Sache
überraschend leicht macht. Der Wunsch ist, später einen anderen Wechselrichter
daranhängen zu können, ohne das Panel anzufassen.

Dieses Dokument ist ein Plan, kein Vorhaben: nichts davon ist entschieden, und
nichts davon ist gebaut. Es sagt, wo die Kopplung heute wirklich sitzt, was
eine zweite Geräteart kostet, und welche drei Entscheidungen vorher fallen
müssen.

Die Reihenfolge der Argumente ist bewusst umgekehrt zu der üblichen: erst die
Bestandsaufnahme, dann das Zielbild, dann die Stufen, und ganz am Ende der
Aufwand.

## Ausgangslage: wo es heute hängt

Fünf Stellen lesen `rctState`, und keine davon kennt das Protokoll:

| Verbraucher | Was er braucht | Code |
|---|---|---|
| Übersicht, Verlauf, Energie, Gerät, Service (GUI) | fast alles | `GuiApp.cpp`, `refreshCb()` |
| Weboberfläche (6 Seiten, 2 JSON-Endpunkte) | fast alles | `WebServer.cpp`, `handleRoot()` |
| Schaltausgang (4 Regeln) | Netz, Fehlerbits, Insel | `Relay.cpp`, `ruleWantsOn()` |
| CSV-Zeile (23 Spalten, alle 5 min) | Leistungen, Temperaturen, Fehlermaske | `sdlog.cpp`, `sdLogSample()` |
| Selbstauskunft (Alter der Daten) | `haveData`, `connected`, `lastUpdateMs` | `DataStatus.h` |

Das ist die gute Nachricht: die Protokollkenntnis liegt fast vollständig in
`RctClient.cpp`. Kein Verbraucher baut Frames, keiner prüft eine CRC, keiner
kennt eine OID.

### Was an *Rückständen* hängen wird

Die schlechte Nachricht ist feiner verteilt. Ein Wechselrichter ist nicht nur
„andere Register": die Werte, die das Panel anzeigt, sind gerechnet, und diese
Rechnungen stehen an mehreren Stellen:

- **Haus = Lastzähler + externer Ertrag.** Weil der Lastzähler des RCT den
  S0-Ertrag nicht sieht. Steht an **fünf** Stellen im C++ — `WebServer.cpp`
  (`loadSum`, eine Kachel), `GuiApp.cpp` dreimal (Flussdiagramm, Verlaufssumme,
  Energie-Seite), `CsvRow.h` (`toSample`) — und einsechsmal im Browser
  (`rpEnergy` in `pages.h`), wo dieselbe Regel aus CSV-Differenzen statt aus
  Zählern kommt.
- **Erzeugung = zwei Strings + S0**, an vier Stellen im C++ und einer im
  Browser. Der Verlauf rechnet hier *ohne* S0 (`toSample`: die beiden Strings
  allein, der S0-Ertrag hat seine eigene Reihe) — dieselbe Zahl erscheint auf
  der Übersicht also mit und im Verlauf ohne S0, und wer das nicht weiß, hält
  es für einen Fehler.
- **Einspeisung als Betrag** (das Gerät liefert negative Zähler) — einmal in
  `GuiApp.cpp`, einmal in `pages.h` im Browser.
- **Eigenverbrauch = Erzeugung − Einspeisung**, dieselbe Regel noch einmal.
- **Vorzeichen**: Netz + = Bezug, Batterie + = Entladung, PV ≥ 0. Gemessen
  dokumentiert in `RctTypes.h` und im Kommentarblock in `GuiApp.cpp`.

Diese Regeln sind nicht „RCT-Logik", sie sind *Anlagen*-Logik. Sie haben in
einer Datei zu stehen, und ein zweiter Treiber darf sie nicht noch einmal
erfinden.

### Was bereits geräteneutral ist

Mehr, als man denkt — das ist der Grund, warum der Plan kurz ausfällt:

| Baustein | Warum er schon passt |
|---|---|
| `DataStatus.h` | fünf Fälle über vier Booleans; kein Wort über RCT |
| `Charts.h` | sechs Reihen, Namen und Farben |
| `NumFmt.h` | Zahlenformate |
| `CsvRow.h` | 23 Spalten plus **Legacy-Regel**: alte Spalten behalten Namen und Reihenfolge, neue kommen hinten an. Das ist schon eine Versionsregel. |
| `i18n` | 359 Texte, alle über IDs |
| `/api/*.json` | stabile Form, Zahlen ohne Einheit im Namen |

Die Perioden-Arithmetik steht allerdings **zweimal**: `energyPeriodValues()` in
C++ für die Energie-Seite, `rpEnergy()` in JavaScript für die Webseite (dort aus
CSV-Differenzen statt aus Zählern). Das ist Absicht und soll so bleiben — zwei
Sprachen, zwei Rechenwege. Was in den Plan gehört, ist die *Regel als Tabelle*,
nicht die Zusammenführung des Codes.

### Was richtigerweise im Treiber steht

Der RCT-Client ist erstaunlich sauber. Drei Eigenheiten des Geräts sind dort
korrekt abgeholt und dürfen mit dem Gerät verschwinden:

- SOC und SOH kommen als Bruchteil (0..1) → Prozent.
- Der Akkustrom hat ein anderes Vorzeichen als die Akkuleistung. Das wird über
  `P = U·I` entschieden, nicht über eine geratene Negation — das hält für beide
  Firmware-Konventionen.
- `prim_sm.island_flag` ist ein Bitfeld; nur Bit 0 ist Inselbetrieb. Gemessen:
  `0x02` im normalen Netzbetrieb, ein Register-Ganzzahl-Boolean liest das als
  „Insel".
- Die S0-Energie wird zusätzlich integriert (Trapezregel, Schrittdeckel), als
  unabhängige Gegenrechnung zum eigenen Zähler des Geräts.

## Zielbild

```
  GUI  ·  Web  ·  Relay  ·  CSV  ·  Verlauf
                    ▲
                    │  plantState (PlantState + PlantCaps)
   ┌────────────────┴─────────────────┐
   │  src/plant/   Regeln, Vorzeichen, │
   │               Perioden, Ableitungen│
   ├──────────────────────────────────┤
   │  src/device/   DeviceDriver.h +   │
   │                Transport.h        │
   ├───────────┬──────────────────────┤
   │ RctDriver │  <zweiter Treiber>   │
   │ +TcpTrans-│  + <sein Transport>  │
   │  port     │                      │
   └───────────┴──────────────────────┘
```

Vier neue bzw. geänderte Bausteine, mehr nicht:

| Datei | Inhalt |
|---|---|
| `src/plant/PlantState.h` | der heutige `RctSnapshot` mit geräteneutralen Namen (`gridImportW`, `loadW`, `generatorW[2]`, `externalW`, …), dazu `PlantCaps` |
| `src/plant/Rules.h` | die eine Regelfassung: Vorzeichen, Summen, Perioden, Selbstverbrauch — **header-only, host-testbar**, im Stil von `DataStatus.h` |
| `src/device/DeviceDriver.h` | die Schnittstelle: `begin()`, `poll()`, Transport-Zugriff, Yield-Hook |
| `src/device/Transport.h` | `open/read/write/close/connected` mit Zeitgrenzen — TCP heute, RS485/Modbus morgen |

Umbenannt wird nichts, was eine Kompatibilitätszusage hat: die NVS-Schlüssel
`rct_host`/`rct_port` bleiben lesbar (ein Release lang), die Web-Routen und die
JSON-Form bleiben unangetastet.

## Der Treibervertrag

Der entscheidende Punkt ist nicht die Anzahl der Methoden, sondern das
Timing-Verhalten. `rctParse()` läuft **im LVGL-Task** und blockiert dort bis zu
4 s; gerettet wird das über `rctSetYieldHook()`, den `main.cpp` mit
`displayLooper()+lv_tick_inc` belegt. Diese Eigenschaft muss der Vertrag
ausdrücklich fordern, sonst baut der zweite Treiber eine Task und die
`rctState`-Absicherung („nur ein Kontext") bricht:

```cpp
struct DeviceDriver {
  // Einmalig: Verbindungsparameter, eigene Puffer. Kein Heap, keine Tasks.
  virtual bool begin(const DeviceConfig &cfg) = 0;
  // Genau ein Abruf. Blockiert höchstens budgetMs, ruft in dieser Zeit den
  // Yield-Hook auf, hält alte Werte, wenn ein Register fehlt.
  virtual void poll(PlantState &out, uint32_t budgetMs) = 0;
  virtual bool connected() const = 0;
  virtual const PlantCaps &caps() const = 0;
};
```

`PlantState` bleibt eine flache Struktur, kein `variant`, keine dynamische
Zuweisung: die Verbraucher kopieren heute Array-Blöcke mit `memcpy`, und das
soll so bleiben. Fehlende Werte werden über `PlantCaps` und ein
Gültigkeitsbit je Gruppe beschrieben, nicht über `haveData` pro Einzelwert.

## Fähigkeiten statt Annahmen

Der Treiber meldet, was er kann. Das ist der Teil, der einen zweiten Gerätetyp
billig macht — heute trägt der Code Annahmen über *dieses* Gerät:

| Fähigkeit | Wo sie ohne das Gerät hineinragt | Was ohne sie passiert |
|---|---|---|
| `Battery` | `haveBattery`, SOC-Kachel, Verlaufsreihe 6 | Akku-Zeile und -reihe aus, Werte „–" |
| `Insel` | `islandKnown`, Relaisregel `Island` | Regel meldet „unbekannt", schaltet aus |
| `Fehlerbits` | `faultBits[4]`, CSV-Spalte `status`, Service | Spalte 0, Text „keine Angabe" |
| `Phasenzahl` | `gridPower[3]`, `loadPower[3]` | einphasig: L2/L3 = 0, Summe stimmt |
| `Externer Ertrag` (S0) | `s0Power`, `e_ext_*`, CSV-Spalte `s0` | Spalte 0, Karte „Eigenverbrauch" unverändert |
| `Temperaturen` | 3 CSV-Spalten, Service-Seite | Spalten 0, Zeilen ausgeblendet |
| `Zwei-Strings-PV` | `pvPower[2]`, `pv_a`/`pv_b`, `totalPvA/BWh` | String B = 0 |

Der Preis ist ehrlich zu benennen: die Fähigkeiten verdoppeln die Anzahl der
Zustände, die die Oberfläche zeigen kann (Wert da, Wert nicht vorhanden). Der
Gegenwert ist, dass ein Wechselrichter ohne Batterie *nicht kaputt* wirkt.

## Der Testbau: aufzeichnen und zurückspielen

Der teuerste Teil des Vorhabens ist nicht der zweite Treiber, sondern die
Prüfbarkeit des ersten. Heute lässt sich der Decoder nur auf echter Hardware
prüfen: `crc_test` prüft die Prüfsumme, aber kein Host-Test prüft, was der
Decoder aus einem echten Antworthaufen macht. Das vorhandene Hilfsmittel ist ein
Simulator, der `rctclient` (GPL-3.0) einbindet und deshalb *außerhalb* des
Repositorys liegt — eine Abhängigkeit, die sich mit jedem Wechselrichter neu
stellt.

Vorgeschlagen ist deshalb eine Aufzeichnung im Projekt:

- `tools/fixtures/rct-session.bin` — ein echter Antwortstrom vom Gerät (ein
  vollständiger Poll, gerne ein zweiter mit S0 und Inselbit gesetzt), roh
  gespeichert.
- `tools/fixtures/rct-session.expected.h` — der `PlantState`, den dieser Strom
  ergeben muss, als Festwert-Vektor.
- `tools/plant_test/` — Host-Test: Strom durch den Decoder, Vergleich gegen den
  Vektor, zusätzlich die Regeln aus `Rules.h` gegen Grenzfälle.
- `tools/rct_replay.py` — ein ~150 Zeilen großer Replayserver mit *eigenem*
  Frame-Kodierer (ein READ ist 8 Byte, die Antwortform steht in `RctClient.cpp`)
  und ohne Fremdpaket. Damit fällt die GPL-Abhängigkeit weg und der Simulator
  wird unabhängig vom Repository benutzbar.

Ergebnis: jede spätere Regeländerung ist ohne Wechselrichter prüfbar, und ein
künftiger zweiter Treiber hat dasselbe Gerüst (Aufzeichnung → Test).

## Stufen

Jede Stufe ist einzeln lieferbar und einzeln nachweisbar. Die Reihenfolge ist
nach Aufwand sortiert, nicht nach Wunschdenken.

| Stufe | Inhalt | Risiko | Nachweis |
|---|---|---|---|
| **1. Regeln bündeln** | `src/plant/Rules.h`: die S0-Summe, die Erzeugungssumme, die Periodenrechnung, die Selbstverbrauchsformel, die Vorzeichen als Tabelle. Web, GUI und `CsvRow` rufen dort auf statt selbst zu rechnen. | niedrig — der Compiler findet jede Stelle, und die Zahl ändert sich nicht | `tools/plant_test` mit den Referenzwerten vom Gerät (PV 5,75 kW / Haus 832 W / Netz +4 W / Akku +810 W als fester Vektor) plus Vergleich Panel ↔ Web auf einer Seite |
| **2. Aufzeichnung + Replay** | `tools/fixtures/`, `plant_test`, `rct_replay.py` | niedrig, berührt keine Firmware | `run_host_tests.sh` läuft grün, `rct_replay.py` beantwortet einen echten Poll |
| **3. Fähigkeiten** | `PlantCaps` einführen; GUI, Relay und CSV fragen sie, statt Annahmen zu treffen; bei RCT alle Bits gesetzt (also: erst einmal nur die Struktur, noch kein sichtbares Fehlen) | mittel — die Oberfläche bekommt Zustände, die sie vorher nicht kannte | Host-Test auf die Fähigkeitsmatrix, manueller Durchgang auf dem Panel |
| **4. Treiberrahmen** | `DeviceDriver.h`, `Transport.h`, `rctParse()` → `devicePoll()`, Fabrik nach Konfiguration; `RctClient` wird `RctDriver` (Datei behält ihren Namen, das erspart Ärger im Build) | mittel — berührt `main.cpp`, aber nur den Aufruf | Gerät unverändert erreichbar; `crc_test` und `plant_test` decken den Treiber ab |
| **5. Zweiter Treiber** | hängt an Stufe 4, Inhalt nicht vorhersehbar | hoch | eigener Testpfad + Gerät in der Hand |

**Empfehlung, und das ist der eigentliche Punkt des Dokuments:** Stufe 5 ist
der Grund für alles andere, also gehört der zweite Treiber *zuerst* in Arbeit —
nicht die Abstraktion. Eine Schnittstelle, die man vor dem zweiten
Anwendungsfall baut, ist zu 80 % geraten. Was ich sofort machen würde, sind
Stufe 1 und Stufe 2: beide kosten überschaubar, beide zahlen sich auch ohne
zweiten Wechselrichter aus (Stufe 1 verhindert die nächste Kopie derselben
Regel, Stufe 2 macht jeden künftigen Umbau ohne Wanduhr prüfbar). Stufen 3 und
4 ohne einen konkreten zweiten Treiber anzufangen wäre Arbeit, die man
zurückbauen kann.

## Was nicht zu tun ist

- **Kein Heap im 10-Sekunden-Takt.** Der Snapshot bleibt eine flache Struktur.
- **Keine eigene Task pro Treiber.** LVGL teilt den Task mit dem Client; eine
  zweite Task bedeutet Sperren für `plantState`. Falls ein künftiges Gerät
  selbst schiebt (MQTT, Push), ist das die *eine* Stelle, an der sich die
  Architektur wirklich ändern muss — und dann ist `RowQueue` aus der
  SD-Historie das vorhandene Muster für die Übergabe.
- **Keine Umbenennung von NVS-Schlüsseln jetzt.** `rct_host`/`rct_port` bleiben
  ein Release lang gelesen, sonst verliert ein Gerät beim Update seinen
  Wechselrichter.
- **Keine Änderung an `/api/*.json`.** Das ist die Zusage nach außen.
- **Keine Zusammenführung der Perioden-Arithmetik mit dem Browser.** Zwei
  Sprachen, zwei Rechenwege, eine dokumentierte Regel.
- **Keine Umbenennung der 23 CSV-Spalten.** Die Legacy-Regel erlaubt nur
  Anhängen. Ob eine zweite Spalte „Quelle" (24) nötig wird, entscheidet sich
  spätestens mit dem zweiten Gerät — dann ist sie genau eine angehängte Spalte.

## Offene Entscheidungen

Vier Fragen. Die erste ist die teuerste und blockiert Stufe 5:

1. **Welcher zweite Wechselrichter?** Ohne Namen kein Register-Set und keine
   Fehlerbilder. Kandidaten, die sich strukturell unterscheiden und deshalb je
   eine eigene Architektur-Spitze zeigen: ein Modbus-RTU-Wechselrichter über
   RS485 (anderer *Transport*), ein Gerät mit Wachstumsschnittstelle Modbus TCP
   (anderes *Antwortverhalten*), ein Gerät ohne Akku (andere *Fähigkeiten*).
2. **Transport.** TCP wie heute, RS485/Modbus, oder beides? Davon hängt ab, ob
   `Transport.h` eine Schnittstelle oder eine Familie sein muss, und ob die
   freien GPIOs des 4848S040 reichen (im Rückblick: 3,3 V, UART vorhanden).
3. **Wie streng ist der Treiber?** Ein Gerät, das ein Register nicht kennt,
   antwortet heute gar nicht — der Client hält den alten Wert und wartet die
   Ruhepause ab. Für ein anderes Gerät ist „Antwortet nicht" versus „0,0 A"
   eine echte Frage, und die Antwort gehört in `Rules.h`, nicht in den
   Bildschirm.
4. **Darf der Log das Gerät nennen?** Wenn ja, eine angehängte CSV-Spalte mit
   der Geräte-ID; wenn nein, bleibt der CSV heute Gerät-los und die Herkunft
   steht nur im Dateinamen.

## Aufwand, grob

| Stufe | Größenordnung |
|---|---|
| 1. Regeln bündeln | klein — ein Tag, plus Test |
| 2. Aufzeichnung/Replay | klein bis mittel — Aufnahme ist ein Halbtag, der Replayserver ein Nachmittag |
| 3. Fähigkeiten | mittel — Struktur klein, Durchgang durch GUI/Relay/CSV ist der Aufwand |
| 4. Treiberrahmen | mittel |
| 5. Zweiter Treiber | hängt vollständig am Zielgerät; erfahrungsgemäß die größte Einzelpostie |

## Literatur zur Schnittstelle

- Registerübersicht: `https://rctclient.readthedocs.io/en/latest/` (die
  OID-Tabelle in `RctClient.cpp` stammt von dort; die RCP-App liest dieselben
  OIDs).
- `src/rct/RctClient.cpp` ist ein Port des `RctParser` aus
  Energy2Shelly_ESP (Apache 2.0). Diese Herkunft gehört zum Treiber und bleibt
  mit ihm zusammen in `NOTICE`, wenn der Treiber in ein eigenes Verzeichnis
  wandert.
- Das externe Projekt `rct-panel-simulator` (GPL-3.0 wegen `rctclient`) ist
  heute die einzige Möglichkeit, das Panel ohne Wechselrichter zu sehen. Stufe 2
  ersetzt es durch etwas, das im Repository liegen darf.