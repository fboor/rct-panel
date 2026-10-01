# Der Schaltausgang (Relais-Port)

Ein Ausgang, eine wählbare Funktion. Das Panel entscheidet, der Benutzer
wählt, *wovon* sich der Ausgang leiten lässt.

Umsetzung: `src/output/Relay.{h,cpp}`, Pins und Polarität in
`src/output/RelayPins.h`. Anzeige und Bedienung auf der Service-Seite
(`src/gui/GuiApp.cpp`), Formular auf der Startseite der Weboberfläche
(`src/web/WebServer.cpp`), zwei Zahlenfelder im Setup-Portal
(`src/config/Configuration.cpp`).

## 1. Die fünf Funktionen

| # | Funktion | Schaltet ein, wenn |
|---|----------|--------------------|
| 0 | Aus (Vorgabe) | nie |
| 1 | Netzbezug > Schwelle | `gridPowerSum` (positiv = Bezug) über der Schwelle |
| 2 | Überschuss > Schwelle | PV A+B minus Hausverbrauch über der Schwelle |
| 3 | Störung | eines der 128 Fehlerbits gesetzt |
| 4 | Inselbetrieb | `islandKnown && islandMode` |

Die Nummern sind zugleich die Reihenfolge, in der die Service-Seite weiterschaltet
(Aus → Netzbezug → Überschuss → Störung → Inselbetrieb → Aus), und zugleich die
Werte des Portalfeldes `relay_mode`.

### Warum die Reihenfolge so ist

`Aus` steht vorn, weil es der Zustand ist, in dem das Gerät ausgeliefert wird
und in dem man es nach einem Experiment wiederhaben will — eine Fehlbedienung
kostet dann nichts. Die beiden Schwellwertfunktionen folgen, weil sie die sind,
für die man das Gerät kauft. Störung und Inselbetrieb stehen hinten: sie sind
speziell, aber sie sind genau die Fälle, in denen ein Ausgang als Alarm
sinnvoll ist, und niemand richtet sie versehentlich ein.

### Der S0-Zähler

Der S0-Wert steht **nur** auf der Verbrauchsseite:

```
Haus        = loadPower[0..2] + s0Power
Überschuss  = pvPower[0] + pvPower[1] − Haus
```

Die Lastmessung des Wechselrichters sieht externe Einspeisung nicht
(`loadPower` ist bereits „Hausverbrauch minus S0-Einspeisung"), deshalb gehört
`S0` in den Hausverbrauch. Würde man es zusätzlich auf die Erzeugungsseite
nehmen, fiele es heraus — und bei einer 2-kW-S0-Anlage mit 1 kW Haus meldete
das Panel 1 kW *eigenen* Überschuss und schaltete eine Last ein, die von
fremdem Strom bezahlt wird. Eigene Erzeugung, eigener Überschuss.

Alternativ gerechnet wird nirgends: `gridPowerSum + batteryPower` ergäbe
dieselbe Zahl, ist aber eine Größe, die sich mit der LadeStrategie des
Wechselrichters ändert, und die liest man als „der Akku ist voll" nicht
„wir haben Überschuss".

## 2. Zeitverhalten

```c
ON_DELAY_MS   = 20000   // Regel muss so lange "an" wollen, dann schaltet sie
MIN_HOLD_MS   = 60000   // einmal an, mindestens so lange an
BAND_PERMILLE =  200    // Hysterese: aus erst unter (Schwelle − 20 %)
EVAL_MS       =  1000   // Regel wird 1 Hz geprüft (RCT-Daten kommen alle 10 s)
DATA_MAX_AGE_MS = 600000// keine frischen Daten -> aus (10 min, siehe unten)
```

Die Verzögerung und die Mindesthaltezeit waren ursprünglich 10 s und 30 s und
sind verdoppelt worden. Grund: eine Wolke vor dem PV-Generator und ein Haus,
das in Lastspitzen zieht und wieder loslässt, erzeugen beide Kreuzungen, an
denen ein kurzes Fenster rattern würde. Ein mechanisches Relais, das alle
wenigen Sekunden schaltet, ist ein Fehlerbericht, keine Funktion.

Die Hysterese ist getrennt davon und liegt bei 20 % der Schwelle: mit 500 W
schaltet der Ausgang bei 500 W ein und bei 400 W aus. Ohne sie würde ein Wert,
der genau auf der Schwelle steht, pro.poll einmal umschalten.

**Keine Daten → aus.** Der Ausgang folgt einem Zustand, den das Panel nicht
kennt. Ein eingeschalteter Ausgang, der wegen eines verschwundenen
Wechselrichters hängen bleibt, wäre die schlechtere Variante — er würde
weiterlaufen, egal was das Gerät zuletzt getan hat. Deshalb ist
`!haveData || Alter > 2 min` ein „aus", und zwar bei *jeder* Funktion.

### Die Datenfrist: 2 min → 10 min (Oktober 2026)

Beobachtet an der Anlage des Anwenders: der Wechselrichter liefert mitunter
mehrere Minuten **keine** Werte, obwohl die TCP-Verbindung die ganze Zeit steht.
Mit den alten zwei Minuten war die Frist damit zu kurz — bei einer Funktion wie
`Netzbezug` schaltete der Ausgang in jeder solchen Pause einmal ab und beim
nächsten Wert wieder ein. Die Regel hat gegen das Gerät gearbeitet, dem sie
folgen soll.

`DATA_MAX_AGE_MS` ist deshalb **600000** (10 min). Die Alternative wäre, die
Altersgrenze ganz abzuschalten und nur noch auf `haveData` zu schalten, also auf
einen tatsächlich geschlossenen Strom; das ist die strengere und für eine
Heizlast die gefährlichere Variante, weil der Ausgang dann auch dann an bliebe,
wenn das Gerät seit Stunden tot ist. Verworfen.

Der Preis der Verlängerung, ausgeschrieben: bis zu zehn Minuten lang handelt der
Ausgang nach einem Wert, der bis zu zehn Minuten alt ist — im Extremfall auch
einmal **ein**, mit einer neun Minuten alten Begründung. Deshalb wird die Zahl
sichtbar als alt gekennzeichnet, statt die Regel still auf einer alten Zahl
laufen zu lassen:

- die **Statusleiste** zeigt nach 60 s ohne neuen Frame `wartet` (gelb) statt
  `aktiv` (grün) — die Werte auf den Seiten sind dann die zuletzt
  eingetroffenen;
- die **Zeile unter dem Ausgang** schreibt `AN · 512 W (letzte Messung)` statt
  `AN · 512 W jetzt` und ist gedämpft;
- die **Weboberfläche** sagt in der Zeile „Wechselrichter“ ebenfalls `wartet`.

Alle drei benutzen dieselbe Schwelle und dieselbe Entscheidung
(`src/DataStatus.h`, geprüft in `tools/badge_test`) — sie können nicht
auseinanderlaufen.

Der Wert steht in `Relay.cpp` und wird nicht in NVS gespeichert. Er ist damit
eine Eigenschaft des Builds und nicht der Anlage — wer eine andere Frist
braucht, braucht eine andere Firmware.

### `wartet` gegen den Simulator prüfen

`wartet` entsteht nur bei einer Verbindung, die steht und schweigt: in
`rctParse()` wird der Socket bei `RCT_RX_TIMEOUT` nur dann aufgegeben, wenn
`rctClient.connected()` false ist. Wird der Simulator einfach beendet, zeigt das
Panel `verbinde neu` — ein anderer Zustand.

Der Simulator kann deshalb beides: `--quiet-after SEKUNDEN` beantwortet ab dem
Zeitpunkt nichts mehr und lässt die Verbindung offen, `SIGUSR1` schaltet
um:

```
tools/rct_sim.py --port 8899 --quiet-after 60   # ab 60 s schweigt er
kill -USR1 <pid>                               # einmal: um, zweimal: an
```

Das Panel muss dafür auf den Simulator zeigen. Dafür gibt es das Build-Flag
`RCT_SIM_HOST` (siehe `Configuration.cpp`); der Aufruf im Kommentar dort ist der
geprüfte. Für den Test besser `Netzbezug` mit hoher Schwelle: dann schaltet
nichts, die Zeile mit `(letzte Messung)` ist aber da.

Nachgewiesen am 1.10.2026 an der Anlage des Anwenders: der Simulator auf einem
Rechner im selben Netz, der Ausgang auf Netzbezug/5000 W, nach 70 s `wartet` in
der Statusleiste des Panels und in der Zeile „Wechselrichter“ der
Weboberfläche. Die Zeile mit `(letzte Messung)` blieb in diesem Lauf offen, weil
der Screenshot die Seite zeigte, auf der das Panel gerade stand.

## 3. Pin und Polarität

`RELAY_PIN 40` — der 1-Wege-Relais-Port der Platine (Aufdruck
„1Way/3WayRelayPort"), der einzige freie Pin des Projekts: Display und Touch
nehmen 3..21/38/39, die SD-Karte 41/42/47/48, 1 und 2 sind die beiden anderen
Relais-Ports der Platine und bleiben für einen zweiten Ausgang frei.

`RELAY_ACTIVE_LOW 0` in `RelayPins.h` — also Schließen bei HIGH. Diese
Angabe folgt nicht aus dem Code, sondern vom Modul; sie ist an der Wand
gemessen worden (2026-09-30, siehe `RelayPins.h`): der Pin liegt im Ruhezustand
auf HIGH, und der Ausgang folgt einem HIGH. ESPHome fährt für diese Platine
`switch: GPIO 40, inverted`, was das Gegenteil behauptet — falls dort etwas
schaltet, ist es diese Konstante. Der Testknopf auf der Service-Seite (5 s an,
5 s aus, zweimal) ist die Prüfung; bleibt das Relais dabei stumm, wird die
Konstante umgedreht und neu geflasht. Sonst ändert sich nichts, weil jeder
Schaltvorgang über `relayWrite()` läuft.

Hardware-Vorbehalt: ein Modul mit aktiver lowscher Triggerung ist *eingeschaltet*,
solange der Pin schwebt — und zwischen Reset und `relayInit()` ist er ein Eingang.
Auf dem Relais-Port der Platze entscheidet die umgebende Hardware; bei einem
selbst verdrahteten Modul gehört 10 kOhm vom Pin auf 3V3 (bzw. auf GND bei
aktiver Hochflanke), damit der Pin beim Hochlauf nicht in der falschen Lage
steht. `relayInit()` ist der erste Aufruf in `setup()` nach den beiden
Diagnoseaufrufen — lange vor `displayInit()` — und legt den Pin vorher noch mit
internem Pull auf die Aus-Stufe, bevor er auf Ausgang gestellt wird.

## 4. Aus bei jedem Start

`relayInit()` schaltet den Ausgang aus, **bevor** die Funktion aus NVS gelesen
wird. Ein Relais, das beim Hochfahren zuschlägt, ist ein Schlag — und bei einer
Heizlast eine Überraschung auf der Rechnung. Dazu kommt: bis die Regel 20 s
stand gehalten hat, wird ohnehin nicht eingeschaltet.

Die Funktion steht in NVS (Namespace `relay`, Schlüssel `mode` und `thresh`) und
überlebt den Neustart. Das ist die ausdrückliche Entscheidung, einstellbar zu
sein und deshalb zu bleiben: jemand, der die Steckdose so konfiguriert hat,
will sie nach einem Stromaus nicht neu einstellen müssen.

## 5. Bedienung, drei Wege

1. **Service-Seite, Antippen** — wechselt die Funktion. Der alltägliche Weg.
   Der Knopf ist wie der Code ein Feld mit Hintergrund, daneben steht
   „antippen = wechseln".
2. **Weboberfläche, Abschnitt „Ausgang"** — Auswahlfeld für die Funktion und ein
   Zahlenfeld für die Schwelle, dazu der Test. Hinter dem Code, weil es das
   Gerät verändert.
3. **Setup-Portal** — `relay_mode` (0..4) und `relay_w` (0..5000) als
   Zahlenfelder. Warum Zahlen und kein Auswahlfeld: `WiFiManagerParameter`
   erzeugt aus seiner ID ein `<input>` (`WiFiManager.cpp`, `HTTP_FORM_PARAM`),
   und ein Parameter ohne ID wird zwar als rohes HTML ausgegeben, bekommt seinen
   Wert beim Speichern aber nicht zurück. Der Weg über Zahlen ist der einzige,
   der im Portal überhaupt funktioniert — und er ist der einzige, den man braucht,
   wenn das Panel nicht im Heimnetz ist und seine eigene Weboberfläche gar
   nicht erreichbar ist.

Neben dem Funktionsnamen zeigt die Service-Seite den Wert, gegen den verglichen
wird („AN · 512 W jetzt"). Ohne diese Zahl ist eine Schwelle in Watt eine Zahl,
die niemand sinnvoll einstellen kann.

## 6. Test

`relayStartTest()` schaltet 5 s ein, 5 s aus, zweimal — ohne Rücksicht auf die
Regel und deshalb auch ohne Daten vom Wechselrichter. Zweck ist in genau einer
Richtung: *ist das der richtige Pin, und stimmt die Polarität?* Ein Test, der
eine laufende Funktion voraussetzt, kann diese Frage nicht beantworten.

Während der Test läuft, hat er den Ausgang; danach startet die Regel bei null
(kein Nachschwingen, keine offene Mindesthaltezeit aus dem Test heraus).

## 7. Der Wirt

`tools/relay_test/` baut `src/output/Relay.cpp` unverändert gegen zwei kleine
Stubs (`millis()` aus einer globalen Variablen, `digitalWrite()` in eine
Variable, die der Test lesen kann, `Preferences` als RAM-Datei). Geprüft sind
die Teile, die auf dem Schreibtisch nicht prüfbar sind: 20 s Einschalt-
verzögerung, 60 s Mindesthaltezeit, 20-%-Hysterese, „keine Daten heißt aus",
der S0-Anteil der Überschussregel, die Testfolge, der Funktionswechsel und
der NVS-Rückweg samt Werten außerhalb des gültigen Bereichs.

```
tools/relay_test/run.sh      # 70 Prüfungen, ~1 s
```

Der Test hat beim ersten Lauf einen echten Fehler gefunden: `s_lastEvalMs
überlebte `relayInit()`. Nach einem zweiten `relayInit()` blieb das
Auswertungsfenster zu, bis `millis()` den alten Wert eingeholt hatte — Minuten,
in denen keine Regel lief. Auf der Platine fällt das nicht auf (einmalig beim
Start, statisch initialisiert), in einem Test Prozess sehr wohl.

Auf der Platine bleibt zu prüfen, was der Host nicht kann: Pin 40 und
Polarität. Dafür der Testknopf.

## 8. Gegen den Simulator prüfen

`tools/rct_sim.py` ist ein Wechselrichterersatz auf Port 8899. Für die beiden
Funktionen, die keinen Störungswort brauchen, ist er das Werkzeug, mit dem die
Schwelle eingestellt wird:

```
tools/rct_sim.py --port 8899                            # Standardlast ~870 W
tools/rct_sim.py --port 8899 --lastung 4                # Haushalt ~2,8 kW
tools/rct_sim.py --port 8899 --faults 0x00000040,0,0,0  # Störung bit 6
```

`--lastung` ist nötig, weil im Simulator nur ein Viertel der Last/PV-Differenz
über den Netzähler läuft (die Batterie nimmt 75 % auf): mit der Standardlast
entstehen höchstens rund 270 W Netzbezug, also nie die 500 W der
Voreinstellung. Erst `--lastung 4` macht den Bezug groß genug.

`tools/rct_sim_test.py` prüft den Simulator selbst, ohne Panel: dass er jede
Kennung beantwortet, die `rctOids[]` in `RctClient.cpp` abfragt, dass die
Bilanz `Last − PV + Batterie = Netz` in jeder Probe aufgeht, dass der
Inselbetrieb wieder endet und dass Überschuss und Netzbezug die
Standardschwelle übersteigen. Läuft mit in `tools/run_host_tests.sh`.

Der Test hat zwei echte Fehler gefunden: der Simulator beantwortete die sechs
`energy.e_ext_*`-Zähler des Wechselrichters nicht (das Panel hätte dort eine
Lücke in der S0-Leiste gezeigt), und der Test selbst übersah fünf Kennungen,
weil sie im Firmware-Array mit sieben statt acht Hexziffern stehen
(`0x3A39CA2`, Last L1). Der Prüflauf liest `rctOids[]` deshalb direkt aus
`RctClient.cpp` und vergleicht die Länge mit `RCT_NUM_SLOTS` — eine Liste im
Test würde genau das verschleiern, wofür er da ist.

Was der Simulator nicht kann: S0 steht auf 0. Ein von außen eingespeister
Generator ließe sich nur einführen, indem er in die Bilanzidentität
eingerechnet wird, und dann wäre die Zahl eine Attrappe.

## 9. Was das nicht ist

- **Kein Sicherheitsgerät.** Der Ausgang schaltet nach einer Regel, die auf
  Messwerten beruht. Er ist kein Fehlerstromschutz, kein Überlastschutz und
  keine Garantie, dass eine Speicherheizung nur mit Solarstrom läuft.
- **Kein Ersatz für die Lastabschaltung des Wechselrichters.** Wer die
  Eigenverbrauchsregelung des RCT nutzt, hat sie bereits.
- **Nur ein Ausgang.** Die Ports 2 und 3 (GPIO 2 und 1) bleiben frei; eine
  zweite Funktion wäre dieselbe Maschine mit einem weiteren `RelayMode`, aber
  sie ist nicht implementiert, weil sie niemand verlangt hat.
- **Der Test schaltet ohne Rückfrage.** Er ist eine Handbetätigung, kein
  Fernbefehl — die Weboberfläche verlangt dafür den Code.
