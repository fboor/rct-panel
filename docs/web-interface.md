# Web-Oberfläche im Normalbetrieb

Zusätzlich zum Einrichtungs-Portal (WLAN „RCT-Panel“, 192.168.4.1) bringt das
Panel im Normalbetrieb einen eigenen Webserver auf Port 80: Statusseite,
CSV-Daten und Screenshots von der SD-Karte, Firmware-Update per Upload.

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
| `/`             | GET                | Übersicht: Netz, PV, Akku, Karte, Ausgang, Adresse, Wartung |
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

## Puffer und Speicher

* 16 kB Stream-Puffer aus PSRAM (`heap_caps_malloc`, Rückfall auf internen RAM)
* 2 kB Listenpuffer im Worker
* 2 kB Sende-Puffer im Webserver
* PROGMEM-Seiten (`src/web/pages.h`), pro Request ~2-3 kB `String` in RAM

Interne Heap-Reserve in Normalbetrieb ~150 kB; Webserver und Worker liegen bei
~2 kB statischem Bedarf darüber. Die Seiten sind aus Flash-Bausteinen
zusammengesetzt (ein Shell-Dokument mit `%T`/`%S`/`%B`-Platzhaltern), nicht aus
`String`-Konkatenation - sonst würde jeder Seitenaufbau einen großen Teil des
Heaps verbrauchen.

## SD-Takt

Die Karte läuft mit 4 MHz (`kSdFastHz`) und fällt auf 400 kHz zurück, wenn der
Rück-Test sie nicht besteht. Begründung und Messung in `docs/sd-history.md`.

## Start und Stopp

`main.cpp` ruft `webStart()`/`webUpdate()` nur in `normalOperation()` auf
(`WIFI_READY` und kein Portal). `Configuration.cpp` ruft `webStop()`, bevor das
Portal das Radio übernimmt. mDNS läuft mit (`rct-panel.local`), ist aber reine
Bequemlichkeit: die IP steht auf der Panel-Seite Service und in der Übersicht,
und ein Netz, das mDNS blockiert, verliert nur den Namen.
