# Den Wechselrichter austauschbar machen

## Worum es geht

Das Panel spricht über genau ein Protokoll mit genau einem Gerät: dem RCT Power,
60 Register über TCP auf Port 8899. Die Abstraktion, um die es hier geht,
versteckt **diese eine Implementierung** hinter einer API, die alle übrigen
Bauteile aufrufen: GUI, Weboberfläche, Schaltausgang, CSV-Logger, Verlauf.

Ausdrücklich im Umfang:

- **Eine** Implementierung, nämlich RCT. Kein zweiter Wechselrichter wird
  geschrieben, geraten oder vorbereitet.
- **Verschiedene Transporte sollen möglich sein**, aber keiner wird umgesetzt.
  Hinter der Schnittstelle liegt eine TCP-Anbindung; die Schnittstelle ist so
  gebaut, dass eine serielle später dazukommt, ohne die Fahrerseite anzufassen.
- **Die Herkunft der Daten steht im Dateinamen** (`RCT-202610.csv`), nicht in
  einer CSV-Spalte. Ein Gerätetyp bekommt ein Kürzel, das Kürzel wandert in den
  Dateinamen, und alle Logs eines Typs liegen nebeneinander.

Was nicht im Umfang ist: ein zweiter Wechselrichter, ein zweites Protokoll, ein
eigener Modbus-Code, eine eigene Task, Änderungen am CSV-Format oder an der
Web-Schnittstelle.

> **Stand heute:** Aus dem zweiten Fahrer ist ein *OpenInverterGateway* geworden,
> und mit ihm `DeviceCaps` — die Anzeige fragt den Fahrer einmal, was seine Familie
> kann, und zeichnet nur das. Siehe [„Was danach dazukam"](#was-danach-dazukam).

## Ausgangslage

Fünf Stellen lesen `rctState`, und keine davon kennt das Protokoll:

| Verbraucher | Was er braucht | Code |
|---|---|---|
| GUI: Übersicht, Verlauf, Energie, Gerät, Service | fast alles | `GuiApp.cpp`, `refreshCb()` |
| Weboberfläche (6 Seiten, 2 JSON-Endpunkte) | fast alles | `WebServer.cpp`, `handleRoot()` |
| Schaltausgang (4 Regeln) | Netz, Fehlerbits, Insel | `Relay.cpp`, `ruleWantsOn()` |
| CSV-Zeile (23 Spalten, alle 5 min) | Leistungen, Temperaturen, Fehlermaske | `sdlog.cpp`, `sdLogSample()` |
| Selbstauskunft über das Datenalter | `haveData`, `connected`, `lastUpdateMs` | `DataStatus.h` |

Kein Verbraucher baut einen Frame, prüft eine CRC oder kennt eine OID. Die
Protokollkenntnis liegt in `RctClient.cpp` — der Ort ist also richtig, nur das
`RCT` steckt im Namen und in der Datei.

Was an *Rückständen* hängt, sind die Regeln. „Haus = Lastzähler + externer
Ertrag" (weil der Lastzähler des RCT den S0-Ertrag nicht sieht) steht an
**fünf Stellen im C++** (`WebServer.cpp` `loadSum`, `GuiApp.cpp` dreimal,
`CsvRow.h` `toSample`) und einsechsmal im Browser. „Erzeugung = zwei Strings +
S0" an vier Stellen im C++ und einer im Browser — und im Verlauf *ohne* S0,
was ohne Kommentar wie ein Fehler aussieht. Die Vorzeichen (Netz + = Bezug,
Batterie + = Entladung, PV ≥ 0) sind gemessen und stehen in Kommentaren neben
den Feldern. Diese Regeln gehören in die API, denn sie sind Anlagenlogik und
keine Gerätelogik.

Drei weitere Stellen nennen das Gerät beim Namen:

| Stelle | Was |
|---|---|
| `sdlog.cpp`, `updatePath()` und `sdWorkerReadHistory()` | `\"/hist/RCT-%s.csv\"`, dreimal als Zeichenkette |
| `Configuration.h` | `extern char rct_host[41]`, `rct_port[6]`, NVS-Schlüssel `rct_host`/`rct_port` |
| Verzeichnis und Dateinamen | `src/rct/RctClient.{h,cpp}`, `RctTypes.h`, `RctCrc.h` |

## Die API

Vier neue Bausteine, ein verschobener. Namen: englisch wie im übrigen Code,
das Wort „Gerät" ist im Panel ohnehin der Wechselrichter (Seite „Gerät").

```
  GUI · Web · Relay · CSV · Verlauf
              ▲   deviceState(), devicePoll(), deviceLoadW(), …
  ┌───────────┴────────────────────────────────────────────┐
  │ src/device/    DeviceState.h   die neutralen Werte      │
  │                DeviceDriver.h  die Fahrerschnittstelle  │
  │                DeviceTransport.h die Transportschnittst. │
  │                Device.cpp      Fabrik, poll, Präfix     │
  ├─────────────────────────────────────────────────────────┤
  │ src/rct/       RctDriver.{h,cpp}, RctCrc.h              │
  └─────────────────────────────────────────────────────────┘
```

| Datei | Inhalt |
|---|---|
| `src/device/DeviceState.h` | der heutige `RctSnapshot` mit geräteneutralen Namen, als `deviceState()` erreichbar |
| `src/device/DeviceDriver.h` | die Fahrerschnittstelle (rein virtuell) |
| `src/device/DeviceTransport.h` | die Transportschnittstelle (rein virtuell) |
| `src/device/Device.cpp` | Fabrik, `devicePoll()`, `deviceTypePrefix()`, die Regelfunktionen |
| `src/rct/RctDriver.{h,cpp}` | die heutige `RctClient.cpp`, als Treiber gegen die Transportschnittstelle |

### `DeviceState`: die neutralen Namen

Die Feldnamen sind der eigentliche Inhalt der Abstraktion — sie sagen, *was*
gemessen wird, nicht welches Register es liefert. Die Einheit steckt im Namen,
weil sie zwischen den Feldern wechselt (ein Zähler in Wh, eine Leistung in W)
und weil die API von mehreren Bauarten benutzt wird.

| heute (`RctSnapshot`) | neu (`DeviceState`) | Einheit |
|---|---|---|
| `gridPower[3]`, `gridPowerSum` | `gridW[3]`, `gridExchangeW` (+ = Bezug) | W |
| `gridVoltage[3]`, `gridFrequency[3]` | `gridV[3]`, `gridHz[3]` | V, Hz |
| `loadPower[3]` | `houseW[3]` | W |
| `pvPower[2]` | `genW[2]` (A, B) | W |
| `s0Power` | `extW` (externer Ertrag, der RCT liest ihn am S0) | W |
| `batteryPower`, `batteryVoltage`, `batteryCurrent`, `batterySoc` | `batW`, `batV`, `batA`, `socPct` | W, V, A, % |
| `dayPvWh`, `monthPvWh`, … 14 Zähler | `dayGenWh`, `monthGenWh`, … | Wh |
| `feedInEnergyWh`, `gridDrawTotalWh` | `feedInTotalWh`, `gridDrawTotalWh` | Wh |
| `dayExtWh`, … und die `Plain`-Variante | `dayExtWh`, … | Wh |
| `batteryStatus`, `faultBits[4]` | unverändert | Bitfeld |
| `deviceName`, `firmwareVersion` | unverändert | Text |
| `coreTemp`, `batteryTemp`, `heatSinkTemp`, `nextCalibTs`, `batteryCycles`, `batterySoh` | unverändert | °C, s, Zyklen, % |
| `islandMode`, `islandKnown`, `haveData`, `haveBattery`, `connected`, `lastUpdateMs` | unverändert | — |

`haveBattery` und `islandKnown` waren Flaggen statt Fähigkeiten: sie sagen „das
Gerät hat noch nicht geantwortet", nicht „das Gerät kann das nicht". Für einen
zweiten Fahrer reicht das nicht, und genau an diesem Unterschied wird ein zweites
Gerät sichtbar — deshalb gibt es jetzt `DeviceCaps` (siehe unten).

### Die Regeln gehören in die API

Weil sie an fünf Stellen stehen, werden sie Funktionen der Abstraktion und
landen in `src/device/Rules.h` — header-only und ohne Arduino, im Stil von
`DataStatus.h`, damit sie der Host-Test prüfen kann:

```cpp
float deviceGridExchangeW();   // Netz, + = Bezug
float deviceGenerationW();     // genW[0] + genW[1] + extW
float deviceHouseW();          // houseW[0..2] + extW
float deviceBatteryW();        // + = Entladung
```

Der Verlauf behält seine eigene Rechnung (`toSample`: Erzeugung ohne S0, S0 als
eigene Reihe), denn das ist eine bewusste Ausnahme und keine vergessene
Stelle.

### `DeviceDriver`

```cpp
class DeviceDriver {
public:
  virtual ~DeviceDriver() = default;
  // Einmalig. Eigene Puffer, kein Heap, keine Task.
  virtual void begin(const DeviceConfig &cfg) = 0;
  // Genau ein Abruf, höchstens budgetMs lang. Muss alte Werte halten, wenn
  // ein Register fehlt, und in dieser Zeit den Yield-Hook aufrufen.
  virtual void poll(uint32_t budgetMs) = 0;
  virtual bool connected() const = 0;
  virtual const char *typeName() const = 0;   // "RCT" - auch der Dateiname
};
```

Die Zeitbedingung ist der Teil, der nicht verhandelbar ist: `rctParse()` läuft
im LVGL-Task und blockiert dort bis zu 4 s; gerettet wird das über
`rctSetYieldHook()`, den `main.cpp` mit `displayLooper()+lv_tick_inc` belegt.
Ein zweiter Fahrer, der das nicht tut, friert das Panel ein — deshalb steht es
im Vertrag und nicht in einer Kopfzeile.

### `DeviceTransport`

```cpp
class DeviceTransport {
public:
  virtual ~DeviceTransport() = default;
  // Millisekunden-Zeitbasis für alle Wartezeiten: eine serielle Schnittstelle
  // rechnet ihre Frame-Pausen aus der Baudrate, nicht aus einem Socket.
  virtual bool open(const char *host, const char *port, uint32_t timeoutMs) = 0;
  virtual size_t write(const uint8_t *buf, size_t n) = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual bool peerOpen() = 0;   // TCP: Socket lebt; RS485: immer true
  virtual void close() = 0;
};
```

Der Schnittstelle folgen vier Eigenschaften, damit eine RS485-Variante später
nichts an der Fahrerseite ändern muss:

- **Bytes, keine Frames.** Rahmen, CRC, Adressierung und Antwortsammlung
  gehören dem Fahrer, nicht dem Transport. Ein Modbus-RTU-Rahmen (Adresse,
  Funktionscode, Register, CRC16, 3,5 Zeichen Pause) ist eine andere
  Rahmensprache als der RCT-Bus mit Escaping — die muss nebeneinander bestehen
  können.
- **`peerOpen()` statt `connected()`.** Beim TCP ist „die Verbindung ist
  zu" eine Meldung des Sockets und ein eigener Fehlerfall (`RctClient` stoppt
  den Socket darauf). Bei RS485 gibt es das nicht.
- **Millisekunden statt eigener Zeitbasis** in der Schnittstelle, damit der
  Fahrer seine Wartefenster selbst rechnen kann.
- **DE/RE gehört in den Transport**, nicht in den Fahrer: das Umschalten der
  Senderichtung ist eine Eigenschaft des elektrischen Anschlusses. Diese
  Schnittstelle hat dafür bewusst keine Methode — sie kommt mit der
  Implementierung dazu, nicht als Leerstelle.

### Konfiguration

`DeviceConfig { char type[12]; char host[41]; char port[6]; }`, aus NVS:

| Schlüssel | Bedeutung | Rückfall |
|---|---|---|
| `device` | Gerätetyp, heute immer `RCT` | `RCT` |
| `device_host` | Adresse des Geräts | `rct_host` |
| `device_port` | Port | `rct_port` |

Der Rückfall ist Pflicht, nicht Kosmetik: ein Panel, das beim Update seine
Adresse verliert, hat danach keinen Wechselrichter mehr, und das fällt
unangenehm auf. Die alten Schlüssel werden ein Release lang gelesen, nicht
mehr geschrieben.

## Der Dateiname als Gerätenachweis

`deviceTypePrefix()` liefert `"RCT"`, aus `RctDriver::typeName()`. Der Logger
baut daraus den Pfad, an zwei Stellen statt an einer mit hartem Text:

```cpp
snprintf(path, sizeof(path), "/hist/%s-%s.csv", deviceTypePrefix(), key);
```

Damit liegen die Logs zweier Gerätetypen nebeneinander und die Zuordnung
steckt im Namen. Eine Spalte im CSV wäre dafür nicht nötig — und wäre
schädlich, weil sie das Format änderte, das die alte Historie noch lesen muss.

Ein Nebeneffekt, den man kennen muss: der Verlaufssucher liest
`<Typ>-<Monat>.csv` und `<Typ>-<Monat davor>.csv`. Nach einem Wechsel des
Gerätetyps findet er die alten Dateien nicht, und der 24-Stunden-Verlauf hat
eine Lücke am Umstelltag. Das ist richtig so: die Zeilen zweier Geräte in
einem Diagramm wären falsch, und der Dateiname ist genau das, woran man es
sehen kann. Der Uptime-Name ohne Uhr (`UPT-<tage>.csv`) bleibt wie er ist, ohne
Präfix — solange die Uhr nicht gültig ist, ist der Typ im Namen noch nicht
wahr.

## Schrittfolge

Alle fünf Schritte sind gebaut (Commits `2be46e5`, `cfc1d42` und der Fahrer-
und Transport-Commit). Kein Schritt hat verändert, was auf dem Display steht;
geprüft wurde das über beide Builds und die Host-Tests, nicht über ein Foto vom
Panel.

| Schritt | Inhalt | Nachweis |
|---|---|---|
| **1. Namen** | `DeviceState.h` mit neutralen Feldern, `deviceState()` als Zugriff; alle fünf Verbraucher umgestellt. Reines Umbenennen, kein Verhalten. | beide Builds grün, Host-Tests grün |
| **2. Regeln** | `Rules.h` mit den Zugriffsfunktionen plus Vorzeichentabelle; `loadSum()` und die fünf Doppelungen sterben, der Verlauf behält seine Ausnahme. `tools/device_test` prüft beide Gerätearten. | 42 Prüfungen grün |
| **3. Transport** | `DeviceTransport` + `TcpTransport`; die `WiFiClient`-Belange wandern aus `RctClient.cpp` in den Transport. Rein mechanisch, das Byte-Protokoll bleibt unangetastet. | `crc_test` grün, Gerät unverändert erreichbar |
| **4. Fahrer** | `DeviceDriver` + Fabrik; `RctClient.cpp` wird `RctDriver.cpp`; `main.cpp` ruft `devicePoll()`. | beide Builds grün |
| **5. Typ im Namen** | `deviceTypeName()` im Logger (zwei Stellen), NVS `device_type`/`device_host`/`device_port` mit Rückfall auf `rct_host`/`rct_port`. | neue Datei heißt weiter `RCT-202610.csv` |

Schritt 1 war der große Diff (Feldnamen in `GuiApp.cpp`, `WebServer.cpp`,
`sdlog.cpp`) und trotzdem der unkritischste: der Compiler findet jede Stelle,
und es ändert sich keine Zahl.

Ein Punkt aus der Liste hat sich beim Bauen als größer erwiesen als gedacht:
die doppelte Periodenrechnung. `energyPeriodValues()` in C++ und `rpEnergy()`
im Browser machen dasselbe aus unterschiedlichen Quellen, und beide mussten an
die Regel angehängt werden — der C++-Teil über `rulePeriod()`, der
Browser-Teil über die Zeilen, die er aus dem CSV liest. Die Regel steht jetzt
an beiden Stellen einmal statt zweimal.

Ein Detail, das erst beim Bauen auffiel: die sechste Kopie der Hausregel war
nicht im Flussdiagramm, sondern in den „Heute"-Karten, die ihre Tageszähler
selbst addierten. Sie war nur deshalb unauffällig, weil sie in derselben Datei
stand wie die anderen.

## Was danach dazukam

Der Plan ist ausgeführt, und zehn Commits später stand der zweite Fahrer da. Was
oben noch als „nicht im Umfang" stand, ist jetzt gebaut — bis auf den Namen, den
das Panel im Log und in der CSV verwendet. Die Reihenfolge war nicht die des
Plans; sie ergab sich aus dem, was sich beim Bauen als zuerst lösend erwies.

### 1. Der zweite Fahrer: OpenInverterGateway

`src/oig/` mit zwei Teilen, die man trennen muss:

- **`OigFields.h`** — die Feldernamen als Daten. Sie sind aus allen sieben
  Growatt-Protokollen extrahiert, weil die Modelle verschiedene Namen für
  dieselbe Größe führen. Das Panel fragt `/status` und liest die Antwort über den
  Namen; es fragt nicht nach einem Register. Dadurch stimmt die Netzfrequenz auch
  bei einem Modell, das sie `grid_freq` statt `fac_frequency` nennt.
- **Die achte Quelle ist ein echtes Gerät.** Ein Growatt MIC 1000 im Test
  weicht von den sieben Protokollen an zwei Stellen ab: die AC-Leistung heißt
  `OutputPower` (nicht `AcPower`), und die Erzeugungszähler heißen
  `TodayGenerateEnergy`/`TotalGenerateEnergy`. Dazu kommen Felder, die es nur bei
  einem Hybrid gibt — und die-panel-lose Frage, ob **überhaupt ein Akku
  angeschlossen** ist: der MIC 1000 ohne Akku meldet `BatteryState` 0, `SOC` 0,
  `ChargePower` 0, `DischargePower` 0 und `BatteryVoltage` 0. Aus „es gibt ein
  SOC-Feld" folgt also nicht „es gibt einen Akku". Die Regel steht als
  `oigBatteryPresent()` im Header, damit der Host-Test sie greifen kann, und
  protokolliert den Rohwert mit.
- **Die Puffergröße war eine Vermutung und ist gescheitert.** `kOigBodyMax`
  stand auf 1024, begründet mit „ein String-Wechselrichter meldet rund vierzig
  Felder". Der MIC 1000 antwortet mit 64 Feldern und 1462 Byte. Die letzten 21
  Felder — **alle Energiezähler** — fielen hinter der Abschneidegrenze weg, und
  das Panel zeigte stundenlang „heute 0,00 kWh, gesamt 0,00 kWh" für ein Gerät,
  das seit Jahren misst. Der Puffer ist jetzt 4096 Byte (3 kB RAM), und eine
  abgeschnittene Antwort steht als `ABGESCHNITTEN` in der Logzeile, die ohnehin
  jeden Zyklus läuft.
- **`OigDriver.cpp`** — der Fahrer. HTTP/1.0 auf dem eingestellten Port (Vorgabe
  8899), ein `GET /status`, dessen JSON über `Json.h` gelesen wird (header-only,
  host-testbar, kein Arduino). Kein `chunked`-Decoder, weil der Stick ohne
  Chunking antwortet.

Das war der Teil mit den echten Überraschungen: Der Stick antwortet auf
`/status` mit **503**, wenn kein Wechselrichter läuft — also sieht ein wacher
Ger-stick ohne Wechselrichter von hier aus genauso aus wie ein schlafender. Der
Fahrer behandelt beides als „schläft" (`DataStatus::Asleep`, der Text sagt
„schläft" statt „keine Daten"), was ehrlicher ist als eine Anzeige, die eine
Antwort behauptet, die keine ist. Die Unterscheidung steht als offene Entscheidung
unten.

### 2. `DeviceCaps`: die Fähigkeiten sind eine Aussage über die Familie

`src/device/DeviceCaps.h`, gesetzt vom Fahrer in `begin()` — nicht aus dem, was
gerade ankam, sondern aus dem, was die Familie kann. Fünf Fähigkeiten und eine
Eigenschaft:

| Feld | Bedeutung |
|---|---|
| `houseMeter` | einen Hauszähler gibt es (sonst kann das Panel keinen Hauswert zeigen — nicht null, sondern nichts) |
| `gridMeter` | einen Netzwechselrichter gibt es |
| `battery` | der Ladezustand antwortet |
| `islandFlag` | das Insel-Flag ist belegt |
| `faultBits` | die vier Fehlerworte sind belegt |
| `sleepsWithoutGeneration` | das Gerät schaltet nachts ohne PV ab (der OIG ja, ein RCT nein — das ist der Unterschied zwischen „schläft" und „hängt") |

Der RCT-Fahrer hat seine Fähigkeiten lange nicht gemeldet und funktionierte nur,
weil ein leeres `caps` zufällig wie „noch nichts bekannt" gelesen wird — was
zufällig das volle Layout bedeutet. Das ist genau die Sorte Zufall, die ein
Gerät mit weniger Zählern sofort verrät, und es ist behoben.

Zwei der Fähigkeiten entscheiden inzwischen auch über **Energiezahlen**, denn
der Eigenverbrauch ist eine Differenz: `houseMeter − gridMeter` geht als
`ruleOwnKnown()` in `Rules.h`. Auf dem MIC 1000 (Haus nein, Netz nein) gibt es
deshalb keinen Eigenverbrauch, keine Autarkie und keine Eigenverbrauchsquote —
vorher stand dort „100 %" aus einer Differenz zweier Nullen, und die Zeile
*Verbrauch 0,00 kWh* las sich wie ein Haus, das nichts braucht. Die Anzeigen
schreiben jetzt einen Strich, und die JSON-Endpunkte liefern `null`.

### 3. Das Diagramm folgt den Fähigkeiten

`src/gui/FlowLayout.h`, header-only und ohne LVGL, damit der Host-Test
`tools/flow_layout_test` (80 Prüfungen) die sechs Fälle prüfen kann. Eine Regel
erzeugt jeden Aufbau:

> **Der Mittelpunkt ist das Haus, wenn es eines gibt, sonst die PV.**

Daraus folgt der Rest: ohne Haus hängt der Akku an der PV, ohne Akku und ohne
Netz ist die PV der einzige Knoten und füllt die Seite — 190 px, mit dem Wert in
der Mitte des Bandes zwischen Kreis und Pille. Was nicht gemessen wird, wird nicht
gezeichnet: kein Knoten, kein Wert, keine Pille. Kein Pfeil ohne Verbindung.

Vier Fakten werden als **ein Byte** in NVS gemerkt (`gui`/`flowcaps`). Grund: Die
Fähigkeiten kommen mit der ersten Antwort, etwa zehn Sekunden nach dem Start. Ein
Diagramm, das sich dann umstellt, sieht auf einem Panel, das man ansieht und
nicht diagnostiziert, wie ein Fehler aus. Also baut die Seite das Layout des
letzten Geräts, das geantwortet hat, und ein Gerät, das etwas anderes sagt, wird
einmal angewandt, protokolliert und für den nächsten Start gemerkt.

### 4. Die Verbrauchsregel ist geräteabhängig — und steht nicht in der API

Das war die eigentlich gefährliche Stelle im Plan: „Hausverbrauch = Lastmessung
plus S0" gilt nur für einen RCT. Ein Gerät hinter einem Stick hat diese S0-Summe
nicht. Deshalb steht die Rechnung nicht in der API, sondern in
`DeviceSemantics` (drei Flaggen, vom Fahrer gesetzt: `loadMeterSeesExternal`,
`genCounterSeesExternal`, `feedCounterNegative`), und die Zugriffsfunktionen in
`Rules.h` fragen sie. Ein Fahrer, der die Rechnung selbst macht, kann sie nicht
vergessen.

### 5. Der Rest, der auffiel

- **Die Zahlform** (`fmtPower()` in `NumFmt.h`): unter 1 kW ganze Watt („380 W“),
  darüber kW mit zwei Stellen („5,75 kW“), Trennzeichen der Sprache. Die Einheit
  wird vom **gerundeten** Wert entschieden, damit 999,5 W nicht als „1000 W“
  gedruckt wird und dieselbe Zahl im nächsten Takt anders aussieht. Der
  Host-Test hat genau diesen Fehler gefunden.
- **Das Symbol** bekam eine eigene Schrift in der Größe, in der es gezeichnet
  wird (`lv_font_mdi_icons_136`): ein 28-px-Zeichen auf 480 % ist ein 134-px-Zeichen
  aus 28 px Information. Bei 190 px Kreis sind das 38 px Luft auf jeder Seite.
- **Der Schaltausgang und die Wartung** sind im Web von der Übersicht auf die
  neue Seite `/einstellungen` gewandert, zusammen mit Gerät und Theme: alles, was
  etwas verändert, auf eine Seite, auf der niemand Werte abliest. Das Panel hatte
  vorher keine Stelle, an der man ein Gerät wechseln kann — siehe Entscheidung 1.
- **Die Karten** im Web folgen denselben Fähigkeiten, die Energiebalken darunter
  nicht (Entscheidung 5).

## Was ausdrücklich nicht gebaut wird

- **Kein zweiter Treiber.** Eine Schnittstelle, die man vor dem zweiten
  Anwendungsfall ausbaut, ist zu einem guten Teil geraten. Der Rahmen wird
  trotzdem jetzt gebaut, weil er billig ist und die Ausarbeitung des
  Fahrers dann nicht mehr aufhält.
- ~~**Kein Fähigkeitsverhandeln.**~~ **Überholt.** Der Plan wollte es zuerst
  nicht; mit dem zweiten Fahrer ist `DeviceCaps` gebaut, und der Fahrer meldet in
  `begin()`, was seine Familie kann. Was *nicht* gebaut ist: eine Verhandlung zur
  Laufzeit. Die Anzeige fragt einmal, und ein Fahrer ändert seine Antwort nicht.
- **Keine neue CSV-Spalte, keine Änderung an `/api/*.json`.** Beides ist eine
  Zusage nach außen.
- **Kein Heap und keine eigene Task** im 10-Sekunden-Takt. LVGL teilt den
  Task mit dem Fahrer; dieselbe Absicherung wie heute gilt. Fällt ein Gerät
  später wirklich mit Push, ist das die eine Stelle, an der sich etwas ändert
  — und dann ist `RowQueue` aus der SD-Historie das vorhandene Muster.
- **Keine Umbenennung der 23 CSV-Spalten.** Die Legacy-Regel erlaubt nur
  Anhängen.

## Offene Entscheidungen

1. ~~**Auswahl des Gerätetyps.**~~ **Erledigt.** Es gibt zwei Fahrer und eine
   Seite `/einstellungen` mit den Feldern *Gerät* (`RCT` oder
   `OpenInverterGateway`), *Adresse*, *Port* und *Theme*. Das Portal kennt nur
   Adresse und Port — der Typ ist Sache des Panels, nicht des Zugangspunkts, weil
   die Liste der Fahrer mit dem Panel wächst.
2. **Wie streng ist der Fahrer?** Ein Register, das der RCT nicht kennt,
   antwortet gar nicht; der Fahrer hält den alten Wert und beendet die Runde
   nach der Ruhepause. Für ein anderes Gerät ist „keine Antwort" gegen „0,0 A"
   eine echte Frage, und die Antwort gehört in den Fahrer, nicht in den
   Bildschirm.
3. ~~**Ob die Geräteart im Portal überhaupt wählbar sein soll.**~~ **Erledigt,
   anders beantwortet:** nicht im Portal (siehe 1), sondern auf einer eigenen Seite
   des Panels.
4. **Was ein schweigendes Gerät bedeutet.** Der OIG-Fahrer kann HTTP 503 nicht
   von „keine Antwort" unterscheiden: ein wacher, sprechender Stick ohne
   Wechselrichter sieht aus wie ein schlafender, und beides heißt heute „schläft".
   Die Unterscheidung gehört in den Fahrer (Statuscode auswerten, ein
   `answered`-Kennzeichen im Zustand) — nicht in die Anzeige.
5. **Energiebalken und Verlaufsreihen.** Sie zeigen weiter alle Größen und
   schreiben für nicht gemessene Zähler Nullzeilen. Das ist bewusst so
   geblieben: Es sind Zähler, keine Messungen, und „0,0 kWh" bei einem Gerät ohne
   Hauszähler ist eine Aussage über den Zähler, nicht über das Haus. Wer es
   trotzdem anders will, braucht eine Entscheidung, keine Vermutung.

## Aufwand

| Schritt | Größenordnung |
|---|---|
| 1. Namen | mittel, rein mechanisch |
| 2. Regeln | klein, plus Test |
| 3. Transport | klein, rein mechanisch |
| 4. Fahrer | klein bis mittel |
| 5. Typ im Namen | klein |

## Literatur

- Registerübersicht des RCT: `https://rctclient.readthedocs.io/en/latest/`
  (die OID-Tabelle in `RctDriver.cpp` stammt von dort).
- `src/rct/RctClient.cpp` ist ein Port des `RctParser` aus Energy2Shelly_ESP
  (Apache 2.0). Diese Herkunft gehört zum Fahrer und wandert mit ihm in
  `NOTICE`, wenn die Datei umzieht.
- `docs/sd-history.md` beschreibt das Format, das die Namensänderung nicht
  anfassen darf.