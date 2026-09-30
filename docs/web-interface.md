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
| `/`             | GET                | Übersicht: Netz, PV, Akku, Karte, Adresse, Wartung |
| `/daten`        | GET                | Liste der CSV-Dateien in `/hist`                    |
| `/daten/<name>` | GET                | eine CSV-Datei, `?tail=<bytes>` für die letzten n Bytes |
| `/bilder`       | GET                | Liste der Screenshots in `/shot`                   |
| `/bilder/<name>`| GET                | ein Screenshot (BMP)                               |
| `/update`       | GET / POST         | Firmware-Update                                    |
| `/aktion`       | POST               | Neustart, WLAN neu einrichten (beide mit Code)     |

Alles andere: 404-Seite.

## Was ohne Code geht und was nicht

Lesen (Übersicht, Listen, Dateien) ist offen - das ist der Zweck der Seite.
Alles, was das Panel verändert, verlangt den 4-stelligen Code:

* Firmware-Update (`/update`)
* Neustart (`/aktion`)
* WLAN neu einrichten (`/aktion`)

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

Abgelehnt wird vor dem ersten geschriebenen Byte: Datei ohne `.bin`, und eine
Größe außerhalb 1 Byte ... 7 340 032 Byte (die app-Partition).

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
