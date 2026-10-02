# Web-Oberfläche im Normalbetrieb

Zusätzlich zum Einrichtungs-Portal (WLAN „RCT-Panel“, 192.168.4.1) bringt das
Panel im Normalbetrieb einen eigenen Webserver auf Port 80: Statusseite mit
Energiebalken, Verlauf mit Diagrammen, CSV-Daten und Screenshots von der
SD-Karte, Firmware-Update per Upload.

## Warum ein eigener Server statt WiFiManager

Der Einrichtungsweg bleibt WiFiManager, aber die Seiten im Normalbetrieb sind
eigene. Grund: `WiFiManager` hält seinen Server in einem privaten
`std::unique_ptr<WM_WebServer>` (`WiFiManager.h`), der Root-Handler lässt sich
also nicht ersetzen. Im Portal-Modus liefert `/` das **WLAN-Formular**, und
dessen Absenden ruft `connectWifi()` - ein normales Aufrufen von `192.168.x.x`
würde das Panel damit aus dem Heimnetz werfen. Ein eigener Server umgeht das,
ohne der Bibliothek etwas zu patchen.

Zweite Bedingung aus derselben Quelle: `startConfigPortal()` prüft nur
`configPortalActive`; ein fremder Server auf Port 80 stört es nicht. Deshalb
ruft `startProvisioningAp()` / `restartProvisioning()` in
`src/config/Configuration.cpp` vorher `webStop()` auf - die beiden Server
teilen sich Port 80 und das Radio, nie gleichzeitig.

## Routen

| Route           | Art                | Zweck                                             |
| --------------- | ------------------ | ------------------------------------------------- |
| `/`             | GET                | Übersicht: Netz, PV, Akku, Karte, Energiebalken, Ausgang, Adresse, Wartung |
| `/verlauf`      | GET                | 24 h und die Historie: Linien- bzw. Banddiagramm, Zeitraumwahl |
| `/api/energie.json` | GET            | die Energiezahlen eines Zeitraums als Zahlen (`?zeitraum=tag\|monat\|jahr\|gesamt`) |
| `/api/verlauf.json` | GET            | der 24-h-Ring aus dem RAM als Zahlen                |
| `/daten`        | GET                | Liste der CSV-Dateien in `/hist`                    |
| `/daten/<name>` | GET                | eine CSV-Datei, `?tail=<bytes>` für die letzten n Bytes |
| `/bilder`       | GET                | Liste der Screenshots in `/shot`                   |
| `/bilder/<name>`| GET                | ein Screenshot (BMP)                               |
| `/update`       | GET / POST         | Firmware-Update                                    |
| `/aktion`       | POST               | `was=neustart`, `was=setup`, `was=ausgang`, `was=test`, `was=bild` (alle mit Code) |

Alles andere: 404-Seite.

## Was ohne Code geht und was nicht

Lesen (Übersicht, Listen, Dateien) ist offen - das ist der Zweck der Seite.
Alles, was das Panel verändert, verlangt den 4-stelligen Code:

* Firmware-Update (`/update`)
* Neustart (`/aktion`)
* WLAN neu einrichten (`/aktion`)
* Funktion und Schwelle des Schaltausgangs (`/aktion?was=ausgang`)
* Test des Schaltausgangs (`/aktion?was=test`)
* Screenshot auslösen (`/aktion?was=bild`)

Der Code wird bei jedem Start neu gezogen (`esp_random()`), steht auf der
Panel-Seite **Service** und ist dort tippbar: ein neuer Code ziehen, wenn jemand
mitlesen konnte. Er ist in NVS **nicht** gespeichert, überlebt also keinen
Neustart - das ist Absicht, ein alter Code soll nicht aus einem Logbuch
wiederverwendbar sein.

Der Code wird vor dem Schreiben geprüft, nicht danach: das Formular auf
`/update` stellt das Code-Feld **vor** das Datei-Feld, weil der WebServer die
Parts der Reihenfolge nach auswertet. Bei `UPLOAD_FILE_START` ist das Feld also
schon da, und ein falscher Code schreibt kein Byte in den Flash.

## Die Diagrammseiten: der Browser zeichnet

**Das Panel zeichnet nichts.** Es liefert Zahlen, der Browser malt daraus Balken
und Linien. Grund ist der Speicher, nicht die Bequemlichkeit:

| Ressource                        | Wert                              |
| -------------------------------- | --------------------------------- |
| interner Heap im Betrieb         | 120 764 Byte frei                 |
| PSRAM                            | 7 588 299 Byte frei               |
| Flash                            | 5,9 MB frei                       |
| Karte                            | ~470 kB/s gemessen                |
| ganze Monatsdatei streamen       | geht heute, rund 3 s              |

Ein serverseitig gerendertes Bild wäre ein Zeichenpuffer plus PNG-Kodierung
(~0,5 MB PSRAM) und mehrere Sekunden Rechenzeit pro Aufruf. Auf einem Gerät mit
480 × 480 Pixeln zusätzlich sinnlos, denn die Diagramme sind so scharf und so
groß wie das Fenster, in dem man sie ansieht.

Die Messung oben ist vom 2.10.2026; sie steht im Boot-Protokoll, mit dem
`stall`-Zähler daneben, weil das die einzige Zahl ist, die sich bei jeder
Änderung an dieser Stelle sofort bewegt.

### Zwei Endpunkte, beide aus dem RAM

`/api/energie.json?zeitraum=tag|monat|jahr|gesamt` liefert die fünf
Energiewerte eines Zeitraums und die beiden Prozente:

```json
{"tz":"CET-1CEST,M3.5.0,M10.5.0/3","period":"day","unit":"Wh",
 "values":{"pv":23680,"own":7790,"feed":15890,"draw":1810,"load":3890},
 "autarky":53.5,"ownShare":32.9}
```

`/api/verlauf.json` liefert den 24-h-Ring, den die Panel-Seite *24 h Verlauf*
zeichnet:

```json
{"tz":"...","points":288,"series":["grid","load","pv","ext","battery","soc"],
 "unit":["W","W","W","W","W","%"],
 "data":[{"t":1790875294,"v":[82,1214,750,0,-480,55]}, null, …],
 "from":1790875294,"to":1790884294}
```

Vier Regeln dazu, jede davon eine Stelle, an der etwas schiefgehen kann:

1. **Ganzzahlig, ohne Exponent.** Die Zähler des Geräts sind ganze Wattstunden;
   `snprintf("%g")` hätte aus einer kleinen Zahl `1e-05` gemacht. Die
   Formatierung steht in `src/web/Json.h`, ohne Arduino-Abhängigkeit, und ist in
   `tools/json_test` geprüft.
2. **`null` statt `nan`.** Ein NaN erreicht `snprintf` als `nan`, und daran
   bleibt ein JSON-Parser stehen - auch der Rest der Antwort wäre weg.
3. **`tz` ist die POSIX-Regel, kein Versatz.** Das Panel stellt seine Zeit mit
   `configTzTime(kTimeZone, …)` (`src/config/Configuration.h`), also mit
   Sommerzeit. Ein fester Versatz wäre ab dem letzten Sonntag im Oktober eine
   Stunde falsch, und die Tagesgrenzen der ganzen Historie verschöben sich genau
   dann um einen Tag. Der Browser rechnet die Regel selbst aus; die Rechnung ist
   in `tools/jstest` gegen Prüfwerte aus Pythons `zoneinfo` geprüft (jede sechste
   Stunde von 2026 und jede Stunde um beide Umstelltage).
4. **`data` ist flach und in Ringordnung**, ältester Punkt zuerst. Eine Lücke ist
   `null` und keine Sechser aus Nullen - so bricht die Linie dort, wo nichts
   gemessen wurde, statt über eine Zeit zu gehen, in der nichts passiert ist.

Die Zahlen kommen aus denselben Rechnungen, die die Anzeigeseiten füllen:
`guiEnergyPeriod()` ruft `energyPeriodValues()`, `guiHistoryPoint()` liest den
Ring, den `histPush()` schreibt. Seite und JSON können deshalb nicht
auseinanderlaufen - dieselbe Disziplin wie `src/DataStatus.h` für das Badge auf
Panel und Web.

Der Ring wird Punkt für Punkt gelesen, nicht kopiert: 288 × 6 Werte plus
Zeitstempel wären 8 kB RAM für eine einzige Anfrage, und die Antwort ist danach
weg. Die JSON-Antwort selbst ist ein `String` mit einer einzigen Reservierung
für die ganze Länge. Gemessen an einer gefüllten Antwort aus dem RAM: **16 kB**
für den Ring (288 Punkte, `?k=` nur gegen den Cache), **163 Byte** für die
Energiewerte. Beides geht in einem Ruck über das WLAN; der Aufwand liegt
eher im Formatieren als im Senden.

### Zwei Zahlen für denselben Tag, und warum

Die Balken auf der Übersicht kommen aus den Zählern des Panels
(`guiEnergyPeriod` → `energyPeriodValues`), die in dessen RAM stehen und **nach
jedem Start bei null beginnen**. Nur der 24-h-Ring wird beim Start aus der
Aufzeichnung zurückgespielt (`sdRequestHistory` → `histPush`), weil er für das
Diagramm gebraucht wird; die Tag-, Monats-, Jahr- und Gesamtzähler nicht.

Die Verlauf-Seite rechnet aus den CSV-Dateien und ist deshalb der Ort für die
Frage „was war am Dienstag". Die Übersicht zählt ab dem letzten Start. Nach einem
Neustart, einem Netzausfall oder einem Tag, an dem der Wechselrichter keine
Daten lieferte, stehen die beiden Seiten also auseinander - die Übersicht zeigt
weniger, der Verlauf die ganze Zahl. Geprüft am 3.10.2026, 01:20: Übersicht Tag
0 kWh (Start um 01:15, keine Daten vom Wechselrichter), Verlauf Tag 02.10.
23,7 kWh erzeugt.

Das ist keine Besonderheit der Weboberfläche: die Panel-Seite *Heute* zeigt
dieselben RAM-Zähler. Wer die Zahl nach einem Neustart braucht, nimmt den
Verlauf.

### Die Energiebalken auf der Übersicht

Unter den vier Karten stehen fünf Balken (Erzeugung, Eigenverbrauch,
Netzeinspeisung, Netzbezug, Verbrauch) mit dem Wert als Text darüber und einem
Zeitraumwechsel **Tag | Monat | Jahr | Gesamt** darüber - Wortlaut, Farben und
Reihenfolge wie auf der Panel-Seite *Energie*.

Sie werden **einmal** geholt, nicht nachgeladen. Grund: weiter unten auf derselben
Seite steht das Formular für die Schwelle des Schaltausgangs, und eine Seite, die
sich selbst neu lädt, überschreibt, was jemand gerade eintippt. Die Werte sind
Zähler, eine Seite von vor zehn Minuten ist im schlimmsten Fall zehn Minuten alt,
und das Nachladen ist ein Fingertipp.

### Die Verlauf-Seite

Vier Bereiche, ein Zustand:

* **24 h** aus `/api/verlauf.json`, ohne Kartenzugriff. Aktualisiert sich selbst
  alle 5 s - anders als die Übersicht ist auf dieser Seite nichts, was jemand
  eintippt, und die neueste Probe kommt alle fünf Minuten.
* **Tag** aus derselben Monatsdatei, als Linie: 288 Punkte, eine Probe alle fünf
  Minuten, eine fehlende Probe eine Lücke in der Linie.
* **Woche** und **Monat** als **Band je Tag** (Tagesminimum bis Tagesmaximum).
  Fünf-Minuten-Punkte über einen Monat als Linie durch Punkte wären eine
  erfundene Genauigkeit; ein Band sagt, was der Tag wirklich hergegeben hat. Die
  sechs Bänder stehen nebeneinander statt übereinander, sonst verdeckten sie
  sich gegenseitig.

Dazu ein Navigator (‹ ›) über die Zeiträume, mit dem Datum in der Mitte. Die
Woche beginnt am Montag, weil das der deutsche Sprachgebrauch ist; das
Jahresdatum steht in der Mitte nur dann, wenn es sich ändert.

### Wie der Browser aus der CSV rechnet

**Die Energie eines Zeitraums ist die Differenz der Lebensdauerzähler zwischen
seiner ersten und seiner letzten Zeile** - nicht die Summe von Momentanwerten.
Das ist genau die Größe, die das Gerät selbst zählt, und sie bleibt über eine
Lücke hinweg richtig. Der externe Generator zählt zur Erzeugung und zum Verbrauch
(dieselbe Rechnung wie `energyPeriodValues`), und der Eigenverbrauch ist, was
geblieben ist: erzeugt minus eingespeist. Die Einspeisezähler kommen am Gerät
negativ an, deshalb wird der Betrag genommen - an einer Stelle, nicht sechsmal.

Die sechs Reihen sind dieselben wie in `csvrow::toSample()`: der Lastzähler des
Wechselrichters hat den S0-Zähler schon abgezogen, deshalb ist der Verbrauch
Zähler plus extern, und die Erzeugung sind beide Strings zusammen.

Die Spaltennamen kommen aus `csvrow::kHeader` und stehen als `data-cols` im
HTML; der Browser liest die Zeilen **nach Namen**, nicht nach Position. Eine
neue Spalte in der CSV ändert damit nichts an dieser Seite, und unbekannte
Spalten werden ignoriert statt geraten.

Die Monatsdateien werden **nacheinander** geholt, nicht nebeneinander: das Panel
bedient einen Download zur Zeit und beantwortet einen zweiten mit „busy". Sie
bleiben danach im Speicher des Browsers, unter ihrem **vollen Dateinamen** als
Schlüssel - ein `RCT-202609.csv` und ein `RCT-202610.csv` sind verschiedene
Dateien, und ein Cache nur über den Monat gäbe die falsche aus. Wer sich drei
Monate weit durchblättert, hat danach eine Datei von ~1,2 MB im Handy liegen und
keinen einzigen weiteren Zugriff aufs Panel.

### Dateien im alten Format

Monatsdateien, die vor dem Wechsel auf 23 Spalten angelegt wurden, tragen 16
Namen über den Zeilen. Hinter der Kopfzeile können aber Zeilen mit 23 Werten
stehen - **das ist der Normalfall und nicht der Ausnahmefall**: auf der
Entwicklerkarte hat `RCT-202610.csv` 231 Zeilen mit 16 und 333 Zeilen mit 23
Werten. Die Entscheidung fällt deshalb **je Zeile**, nicht je Datei; eine Meldung
aus der Kopfzeile würde Tagen ohne Summen nennen, die welche haben.

Der Browser füllt die fehlenden Summen mit **0** - genau wie der Panel-Leser es
macht (`csvrow::parse()`: ein Zähler, der nicht geloggt wurde, liest sich als 0,
und nur die eine Stelle, die das weiß, darf das sagen). Damit bleibt die Ansicht
durchgehend befüllt und der Browser rechnet ohne Sonderfall. Damit die Null nicht
als Messwert gelesen wird, steht ein Satz über dem Diagramm, sobald **im
gewählten Zeitraum** eine Zeile ohne Summen liegt:

> Teile der Zeilen haben keine Summen (von vor dem Update): Tage ganz davor
> zeigen 0, ein Zeitraum über den Wechsel beginnt mit der ersten Zeile, die
> Summen hat.

Drei Fälle, und keiner von ihnen erfindet eine Zahl:

* **Der Zeitraum hat überall Summen.** Die Differenz zwischen seiner ersten und
  seiner letzten Zeile - dieselbe Größe wie im Kapitel über die Energie-Seite.
* **Der Zeitraum hat keine.** Dann gibt es nichts zu subtrahieren: die Balken
  zeigen 0, und die beiden Prozente stehen als **–**. Ein Zeitraum ohne Zähler
  hat keine Quote, und „100 % Eigenverbrauch" wäre eine Antwort auf eine Frage,
  die niemand gestellt hat. Das trifft auch den ersten Tag nach dem Update,
  wenn er nur eine einzige Zeile mit Summen hat: ein Zähler braucht zwei
  Ablesungen, bevor er etwas sagt.
* **Der Zeitraum hat beide.** Dann läuft die Differenz von der ersten Zeile
  **mit** Summen bis zur letzten mit Summen, nicht von der ersten Zeile des
  Zeitraums - sonst wäre sie die Differenz zwischen einem Zähler und einer Null,
  also dessen ganzes Leben statt der Energie dieses Zeitraums. Was am Anfang des
  Zeitraums fehlt, steht in dem Satz über dem Diagramm.

Für eine Anlage, die später auf die Firmware kommt, gibt es den Fall nicht.

### Lücken

Gezählt wie auf dem Panel (`histPush` in `src/gui/GuiApp.cpp`): eine Probe, die
mehr als eineinhalb Intervalle zu spät kommt, heißt, dass die Plätze dazwischen
nie geschrieben wurden. Angezeigt wird derselbe Satz wie am Panel („9 Lücken,
110 min ohne Messwerte", `T_D_GAP_MANY`). Ohne diese Zeile liest sich eine
Aufzeichnungspause wie ein Einbruch.

Im 24-h-Bild sind die Lücken die leeren Plätze des Rings, also direkt
gezählt - dieselbe Zahl, die das Panel unter seinem Diagramm zeigt.

### Was das kostet und was nicht geht

* **Kein Nachladen auf `/`.** Aus dem Grund oben.
* **Kein Server-Rendering, keine Bibliothek, kein CDN.** Die Seite läuft im
  lokalen Netz; ein Nachladen aus dem Internet würde genau die Seite brechen, die
  zeigt, ob das Panel noch lebt. Handgeschriebenes SVG und DOM, zusammen
  `src/web/pages.h`: 18,4 kB CSS, 11,8 kB Logik, 17,8 kB Script. In der
  Flash-Rechnung ist das eine Zeile (5,8 MB frei); der eigentliche Preis ist die
  Prüfbarkeit, nicht der Platz - und die ist mit `tools/jstest` bezahlt.
* **Keine Texte im Script.** Beschriftungen, Überschriften und der Fehlsatz
  kommen als `data-*`-Attribute aus der Firmware, sonst könnte ein Wort auf der
  Seite anders heißen als in der Sprachtabelle. Dasselbe gilt für das
  Datumsformat (`{D}.{M}.{Y}` oder `{Y}-{M}-{D}`), die Trennfarbe und die
  sechs Reihenfarben.
* **Einheiten an den Achsen, weil hier Platz ist.** Jede Skalenmarke links
  trägt die Einheit hinter der Zahl (`10,0 kW`), und rechts am Rand steht die
  Skalierung des Ladezustands von `0 %` unten bis `100 %` oben - geschrieben,
  wie man es auch im Text schreiben würde. Auf dem 480-Pixel-Display fehlt dafür
  der Platz; die Marken stehen dort ohne Einheit, die Legende nennt die Reihen.
  Die Einheit des Ladezustands kommt aus der Antwort des Panels, ist also die,
  die er geschickt hat.
* **Die Oberfläche steht während eines Dateizugs.** Bei der gemessenen Rate von
  ~470 kB/s sind die ~1,2 MB einer Monatsdatei rund 2,6 s Lesezeit, in denen die
  Bedienung des Panels wartet - dieselbe Arbeit, an der die Oberfläche beim
  Schreiben der CSV-Zeile schon 4,4 s stillsteht. Der 24-h-Bereich ist davon
  nicht betroffen: er kommt aus dem RAM.
* **Ein Zeitraum mit zwei Monatsdateien** (eine Woche über den Monatswechsel)
  lädt beide, der zweite erst, wenn der erste fertig ist.

### Prüfung

* `tools/jstest` schneidet den Logikblock aus `src/web/pages.h` heraus und führt
  ihn in node aus - **derselbe** Code, der im Browser läuft, keine Abschrift.
  Geprüft werden die Zeitzonenregel gegen `zoneinfo`, das Lesen der CSV, die
  Tagesbereiche, die Monatsrechnung und die Energiedifferenz an den Zahlen vom
  2.10.2026 (23,68 kWh erzeugt, 15,89 kWh eingespeist, 7,79 kWh Eigenverbrauch,
  1,81 kWh Bezug, 3,89 kWh Verbrauch).
* `tools/json_test` prüft die Zahlenformatierung der Antworten.
* Die Seiten wurden im Browser gegen beide Spaltenformate bei 360 px und 1024 px
  Breite angesehen, in beiden Sprachfassungen.

## Daten aus der SD-Karte: Stream statt Datei im RAM

Die Karte gehört dem Worker aus `src/storage/sdlog.cpp`; der Webserver fasst
sie nie an. Ein Download ist eine Handshake über drei Aufrufe:

```
sdRequestStream(path, tailBytes)   // anfordern, kehrt sofort zurück
sdStreamTotal()                    // Byte-Zahl, sobald der Worker die Datei geöffnet hat
sdTakeStreamChunk(buf, max)        // 0 = noch nicht fertig, -1 = fertig/Fehler
sdStopStream()                     // abbrechen
```

Der Worker füllt **einen** 16-kB-Puffer pro Durchlauf und nur, wenn der
vorige abgeholt wurde. Ein Browser, der langsam liest, hält dadurch den Worker
an - endliche Puffer, kein Wachstum. Umgekehrt wartet eine geparkte
5-Minuten-CSV-Zeile höchstens einen Puffer (≈40 ms bei 4 MHz), Logging und
Download hungern sich nicht aus.

`Content-Length` ist die echte Dateigröße, nicht das, was gelesen wurde: ein
abgebrochener Download ist damit im Browser ein fehlgeschlagener Transfer und
keine stillschweigend gekappte Datei.

### Zwei Dinge, die die Bibliothek sonst ruiniert hätten

1. **Fünf Sekunden.** `WebServer::handleClient()` lässt die Verbindung
   `HTTP_MAX_DATA_WAIT` = 5000 ms offen und schließt sie dann. Eine ganze
   CSV-Monatsdatei braucht länger. Solange ein Download läuft wird
   `handleClient()` deshalb **nicht** aufgerufen - nur so bleibt der Socket
   offen. Preis: während eines Downloads bedient das Panel keine zweite
   Anfrage. Ein Verzeichnis, das 999 Screenshots enthält, passt nicht in einen
   2-kB-Puffer; die Seite sagt das, statt eine gekürzte Liste als vollständige
   auszugeben.
2. **`sendContent()` gibt nichts zurück.** In dieser Core-Version ist der Rückgabewert
   `void`; ein partieller Write (volles TCP-Fenster) ginge als Datenverlust
   durch. Der Rumpf geht deshalb über eine eigene Kopie des Clients:
   `WiFiClient` ist referenzgezählt (`shared_ptr`-Socket-Handle), die Kopie
   redet mit demselben Socket und `write()` liefert die wirklich geschriebene
   Zahl. Damit ist auch der Fall "Browser ging weg" überhaupt erkennbar
   (4 s ohne Fortschritt → Stream abbrechen, Karte freigeben).

Ein Write blockiert nie: `WiFiClient::write()` nutzt `select()` mit 35 ms
Timeout und `MSG_DONTWAIT`, höchstens vier Versuche - im schlechtesten Fall
~140 ms, und nur während ein Browser nicht liest.

## Verzeichnislisten

`sdRequestListing(dir)` / `sdTakeListing(out, cap)` - eine Zeile pro Eintrag,
`name|size|epoch`, in Verzeichnisreihenfolge. Auch das über den Worker, aus
denselben Gründen. Die Seite hält die Antwort nicht auf: der Handler fordert an
und geht ohne Antwort zurück, gerendert wird, sobald der Worker sie hat
(die Verbindung lebt 5 s, der Worker braucht wenigezig Millisekunden). Nach
4 s ohne Antwort: 503.

Der Puffer ist 2 kB (~60 Zeilen). Läuft er mitten in einer Zeile voll, sagt die
Seite das — die Prüfung auf das letzte Zeichen muss aber **vor** dem Rendern
passieren: der Renderer ersetzt die Zeilenumbrüche im Puffer durch `\0`, danach
ist das letzte Byte immer `\0` und die Meldung stünde auf jeder Seite mit
Dateien.

## Screenshot auslösen (`/bilder`)

Unter der Bildliste steht ein Formular (Code + **Screenshot auslösen**), das an
`/aktion?was=bild` geht. `guiRequestShot()` nimmt die Aufnahme **ohne** die
5-s-Vorlauf der Panel-Taste: die gibt es dort, weil man vorher noch auf die
richtige Seite blättern muss — im Browser ist die gewünschte Seite bereits die
sichtbare.

Die Antwort ist ein 303 auf `/bilder?neu=1`, und `?neu` zählt die Schritte des
**einen** Reloads, der zu einer Aufnahme gehört:

| Schritt | Adresse          | Was passiert                                        |
| ------- | ---------------- | --------------------------------------------------- |
| 1       | `/bilder?neu=1`  | dieselbe Liste wie vorher, dazu ein Meta-Refresh nach 6 s |
| 2       | `/bilder?neu=2`  | **ein** Reload, der die Karte neu liest — danach steht die Seite still |

6 s, weil das Schreiben von 691 kB bei 4 MHz gemessen 3 s und (auf einer Karte,
die parallel die CSV-Zeile schrieb) 5 s brauchte. Schritt 2 liest das Verzeichnis
mit `sdRequestListing(dir, /*force=*/true)`: ohne das würde der bis zu 5 s alte
Listenpuffer die gerade geschriebene Datei noch zurückhalten. Läuft das Schreiben
dann noch (langsame Karte), fragt Schritt 2 ein weiteres Mal nach, höchstens
`kShotReloadMax` (8) Schritte — danach ist der Knopf **Seite neu laden** da.
Eine Seite, die sich endlos selbst neu lädt, ist unlesbar; deshalb genau einer.

Ein unausgeschriebener Schreibvorgang ist ausgeschlossen: `sdWorkerWriteShot()`
prüft jeden `write()` und wiederholt einen kurzen bis zu viermal, vergleicht
danach die Dateigröße auf der Karte mit der beabsichtigten und **löscht** eine
Datei, die zu kurz ist. Auf der Wand gemessen waren 3 von 8 Aufnahmen kurz
(0, 167 kB, 499 kB von 675 kB), während der Rückgabewert ungelesen blieb und die
Log-Zeile jedes Mal die Sollgröße meldete. Eine zu kurze Datei gilt im
Webserver als „gibt es nicht" (404), nicht als Lesefehler der Karte.

## Firmware-Update (OTA)

`Update.begin/write/end` aus einer einzigen Kontext (`loop()`), denn der
`Updater` hat keinen eigenen Mutex. Der Updater löscht verzögert: `begin()`
wählt nur den Zielslot und allokiert 4 kB, das Löschen passiert pro geschriebenem
64-kB-Block in `_writeBuffer`. Der längste Einzelsystem ist damit ein
Block-Löschen in zig Millisekunden, nicht ein mehrsekündiges Partition-Löschen -
das Panel bleibt während des Schreibens bedienbar.

Ein mißlungenes Update brickt nichts: das Image wird vor dem Booten
magisch-byte-geprüft, der andere 7-MB-Slot bleibt unangetastet, schlimmstenfalls
bootet das Panel die alte Firmware.

Abgelehnt wird vor dem ersten geschriebenen Byte: der Code und ein Dateiname
ohne `.bin`. Zwei Eigenschaften dieser WebServer-Version haben das Update vorher
**immer** scheitern lassen, ohne dass ein Byte geschrieben wurde:

- `HTTPUpload::totalSize` ist bei `UPLOAD_FILE_START` **0** — die Bibliothek
  summiert die Chunkgrößen erst beim Durchlaufen auf und kennt die
  `Content-Length` der Anfrage nicht. `Update.begin(0, ...)` ist ein
  `UPDATE_ERROR_SIZE`, also stand im Log immer „Update abgelehnt (Groesse passt
  nicht)". Jetzt `Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)`: der ganze Slot,
  und `end(true)` schneidet das Image auf das, was wirklich ankam. Die
  Größengrenze wird deshalb **während** des Schreibens geprüft
  (`s_otaBytes + currentSize > kMaxFirmware` → `Update.abort()`), nicht davor.
- `HTTPUpload::name` ist der **Formularfeldname**, nicht der Dateiname; für
  dieses Formular sind beide `fw`. Die `.bin`-Prüfung gehört auf `filename`, sonst
  wird jede Datei abgewiesen (Log: „Update abgelehnt (keine .bin-Datei)").

Beides auf der Wand geprüft: 1 472 560 Byte in 4534 ms, danach Neustart mit
`rst:0xc`, neuer Wartungscode, Karte und Werte wieder da.

## Schaltausgang

Die Startseite trägt oben Zustand und Funktion des Ausgangs, darunter das
Formular: Auswahlfeld für die Funktion (5 Werte), Zahlenfeld für die Schwelle in
Watt, und zwei Knöpfe - `was=ausgang` (übernehmen) und `was=test` (5 s an,
5 s aus). Die Regeln dahinter stehen in `docs/relay.md`.

`was=test` antwortet sofort und startet den Test im Hintergrund: der Browser
würde 20 s lang auf eine Antwort warten, die er nicht braucht. Ein zweiter
Teststart während eines laufenden Tests ist ein 409, kein zweiter Test.

`was=ausgang` liest beide Felder und speichert die Schwelle auch dann, wenn die
gewählte Funktion keine hat - so steht der Wert beim Zurückwechseln auf die
Schwellwertfunktionen noch da. Die Service-Seite kann dieselbe Funktion durch
Antippen wählen, aber keine Schwelle: Zahlen in Watt brauchen eine Tastatur, und
das Panel hat nur ein Touchscreen.

## Sprache

Der sichtbare Text steht in `src/i18n/`, eine Tabelle je Sprache, und wird zur
Bauzeit gewählt: `pio run -e esp32-s3` ist Deutsch, `pio run -e esp32-s3-en`
englisch (`-DRCT_LANG_EN`). Zur Laufzeit gibt es keinen Umschalter - die
Anzeigeseiten werden einmal in `guiStartApp()` gebaut, und eine zweite Tabelle
wäre für ein paar kB Text ein zweiter Zustand, den man auf der Wand nicht
prüfen kann.

Für die Webseiten heißt das: `tr(T_...)` statt eines deutschen Literals, und der
`<html lang>`-Wert kommt aus derselben Tabelle (`T_HTML_LANG`). Zwei
Konventionen in den Tabellen: Web-Texte tragen HTML-Entities (`&uuml;`),
Anzeigetexte sind normales UTF-8 (Montserrat hat die Umlaute). Geprüft wird das
in `tools/i18n_test`: gleiche IDs und gleiche Platzhalter auf beiden Seiten,
kein deutscher Buchstabe in `src/web/` und `src/storage/`.

Für die Diagrammseiten kommt derselbe Weg über den Browser: Die Worte, das
Datumsformat und die Trennfarbe stehen als `data-*`-Attribute im HTML, das Script
liest sie. Ein Wort, das im Script stünde, wäre beim nächsten Übersetzen eine
zweite Stelle, an der es falsch werden kann.

Nicht übersetzt sind das Serienprotokoll (Entwicklertext, bleibt wie er ist) und
die CSV-Spaltenköpfe (`ts,pv_a,...` - eine Tabelle in Excel darf ihre Spalten
nicht mit der Anzeigesprache wechseln).

## Puffer und Speicher

* 16 kB Stream-Puffer aus PSRAM (`heap_caps_malloc`, Rückfall auf internen RAM)
* 2 kB Listenpuffer im Worker
* 2 kB Sende-Puffer im Webserver
* PROGMEM-Seiten (`src/web/pages.h`), pro Request ~2-3 kB `String` in RAM
* JSON-Antworten: 163 Byte für die Energiewerte, ~16 kB für den 24-h-Ring,
  jeweils eine einzige Reservierung für die ganze Antwort

Interne Heap-Reserve in Normalbetrieb ~150 kB; Webserver und Worker liegen bei
~2 kB statischem Bedarf darüber. Die Seiten sind aus Flash-Bausteinen
zusammengesetzt (ein Shell-Dokument mit `%T`/`%L`/`%R`/`%S`/`%J`/`%B`-Platzhaltern,
`%J` ist das Diagrammscript und bleibt auf den Seiten ohne Diagramm leer), nicht
aus `String`-Konkatenation - sonst würde jeder Seitenaufbau einen großen Teil des
Heaps verbrauchen.

Der Verlauf hat die Diagrammseite in **zwei** Teile geteilt, und das ist der
eigentliche Speichergrund: die Übersicht lädt ihre Datei nicht, sie fragt nach
Zahlen aus dem RAM. Der Verlauf lädt genau eine Monatsdatei - und die liegt im
Speicher des **Browsers**, nicht im Panel. Statisch ist durch die Diagrammseiten
nichts dazugekommen (112 264 Byte, vor und nach der Änderung gemessen).

## SD-Takt

Die Karte läuft mit 4 MHz (`kSdFastHz`) und fällt auf 400 kHz zurück, wenn der
Rück-Test sie nicht besteht. Begründung und Messung in `docs/sd-history.md`.

## Start und Stopp

`main.cpp` ruft `webStart()`/`webUpdate()` nur in `normalOperation()` auf
(`WIFI_READY` und kein Portal). `Configuration.cpp` ruft `webStop()`, bevor das
Portal das Radio übernimmt. mDNS läuft mit (`rct-panel.local`), ist aber reine
Bequemlichkeit: die IP steht auf der Panel-Seite Service und in der Übersicht,
und ein Netz, das mDNS blockiert, verliert nur den Namen.
