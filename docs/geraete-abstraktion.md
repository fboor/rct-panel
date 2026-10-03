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

Was nicht im Umfang ist: Fähigkeiten aushandeln (die bestehenden Flags
`haveBattery` und `islandKnown` bleiben, wie sie sind), ein zweiter Treiber,
Modbus-Code, eine eigene Task, Änderungen am CSV-Format oder an der
Web-Schnittstelle.

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

`haveBattery` und `islandKnown` bleiben Flaggen statt Fähigkeiten: sie sagen
„das Gerät hat noch nicht geantwortet", nicht „das Gerät kann das nicht". Das
unterscheidet einen zweiten Treiber von einer zweiten Geräteart.

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

## Was ausdrücklich nicht gebaut wird

- **Kein zweiter Treiber.** Eine Schnittstelle, die man vor dem zweiten
  Anwendungsfall ausbaut, ist zu einem guten Teil geraten. Der Rahmen wird
  trotzdem jetzt gebaut, weil er billig ist und die Ausarbeitung des
  Fahrers dann nicht mehr aufhält.
- **Kein Fähigkeitsverhandeln.** `haveBattery` und `islandKnown` bleiben das,
  was sie sind. Ein Gerät ohne Akku ist eine spätere Entscheidung.
- **Keine neue CSV-Spalte, keine Änderung an `/api/*.json`.** Beides ist eine
  Zusage nach außen.
- **Kein Heap und keine eigene Task** im 10-Sekunden-Takt. LVGL teilt den
  Task mit dem Fahrer; dieselbe Absicherung wie heute gilt. Fällt ein Gerät
  später wirklich mit Push, ist das die eine Stelle, an der sich etwas ändert
  — und dann ist `RowQueue` aus der SD-Historie das vorhandene Muster.
- **Keine Umbenennung der 23 CSV-Spalten.** Die Legacy-Regel erlaubt nur
  Anhängen.

## Offene Entscheidungen

1. **Auswahl des Gerätetyps.** Umgesetzt ist die neutrale Beschriftung
   („Address", „Port", Abschnitt „Inverter options"); ein Feld für den Typ gibt
   es noch nicht, weil es eine Liste mit einem Eintrag wäre. Es wird eine, sobald
   es zwei Treiber gibt — und dann ist es eine Zeile in der Fabrik.
2. **Wie streng ist der Fahrer?** Ein Register, das der RCT nicht kennt,
   antwortet gar nicht; der Fahrer hält den alten Wert und beendet die Runde
   nach der Ruhepause. Für ein anderes Gerät ist „keine Antwort" gegen „0,0 A"
   eine echte Frage, und die Antwort gehört in den Fahrer, nicht in den
   Bildschirm.
3. **Ob die Geräteart im Portal überhaupt wählbar sein soll.** Wenn ja, ist das
   eine Liste mit einem Eintrag je implementiertem Fahrer — heute also genau
   einer. Sobald ein zweiter existiert, ist es eine Zeile in der Fabrik.

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