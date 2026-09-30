# RCT Power Panel <span class="h-sub">Benutzerhandbuch</span>

Das **RCT Power Panel** ist ein Wandpanel (4-Zoll-Farb-Touchdisplay) zur
Anzeige der Live-Daten Ihres RCT-Power-Wechselrichters. Es liest die Werte
direkt über das Netzwerk aus dem Wechselrichter (TCP, Standard-Port 8899),
zeigt sie auf sieben übersichtlichen Seiten an und zeichnet die Messwerte
zusätzlich automatisch auf einer microSD-Karte auf.

<figure class="ports-shot">
  <img src="img/anschluesse.png" alt="Schnittstellen im Überblick: Touch-Display, microSD-Steckplatz, USB-C-Anschluss">
  <figcaption>Bild 1: Vorder- und Rückseite des Panels mit Touch-Display, microSD und USB-C</figcaption>
</figure>

---

## Kurzanleitung <span class="h-sub">Inbetriebnahme in 5 Schritten</span>

Sie brauchen nur **zwei Angaben**, sonst nichts weiter zu wissen:

1. den **Namen und das Passwort Ihres WLAN**,
2. die **IP-Adresse Ihres RCT-Wechselrichters** — das Gerät zeigt sie
   gelegentlich direkt auf seinem Display an.

Der Port ist einheitlich `8899` und bereits voreingestellt — dort ist
nichts einzutragen.

So geht's:

1. Panel anschließen — USB-C-Kabel an ein Netzteil, das Display zeigt
   sofort die Übersicht.
2. WLAN „RCT-Panel" wählen — den Zugangspunkt erzeugt das Panel beim
   ersten Start (oder wenn kein gespeichertes Netzwerk erreichbar ist).
3. Portal öffnen — im Browser `http://192.168.4.1` aufrufen (die
   Konfigurationsseite öffnet sich meist von selbst).
4. Zwei Felder ausfüllen und speichern — WLAN-Name/-Passwort sowie die
   RCT-IP-Adresse (der Port ist bereits voreingestellt).
5. Fertig. Das Panel verbindet sich mit Ihrem WLAN und zeigt die
   Live-Daten. Der Zugangspunkt „RCT-Panel" verschwindet dabei von selbst.

So sieht es danach aus: Oben die Statusleiste mit dem Verbindungsstatus
(`live`, grün = alles gut), in der Mitte die aktuelle Seite; unten blättern
◀ / ▶ durch die sieben Seiten, ⌂ springt zur Übersicht. Details
zu den Seiten stehen in Kapitel 4, zur Einrichtung ab Kapitel 2.

---

## 1. Technische Daten

| Bezeichnung | Technische Daten |
|---|---|
| Anzeige | 4" IPS-Farbdisplay, 480 × 480 Pixel (ST7701 RGB) |
| Bedienung | kapazitives Touchpanel (GT911) |
| Prozessor | ESP32-S3, Dual-Core |
| Speicher | 16 MB Flash, 8 MB PSRAM |
| Datenspeicher | microSD/TF-Karte, FAT32 (Steckplatz auf der Platine) |
| Stromversorgung | USB-C, 5 V DC |
| WLAN | IEEE 802.11 b/g/n (2,4 GHz) |
| Datenabfrage | RCT-Wechselrichter über TCP (Port 8899) |
| Datenaufzeichnung | alle 5 Minuten als CSV (ca. 40 KB pro Tag) |

Das Panel zeigt ausschließlich Messwerte an — es **verändert keine
Einstellungen am Wechselrichter** (eine Ausnahme: der Setup-Modus legt nur
die eigenen Netzwerk- und Verbindungsdaten des Panels fest).

### Datenfluss

- Das Panel liest alle Live-Werte alle 10 Sekunden aus dem Wechselrichter.
- Das Display aktualisiert sich einmal pro Sekunde mit den zuletzt
  gelesenen Werten.
- Der 24-Stunden-Verlauf nimmt alle 5 Minuten einen Messpunkt auf.
- Ist der Wechselrichter nicht erreichbar, zeigt das Panel weiterhin die
  letzten Werte an, kennzeichnet den Zustand aber im Statusfeld (siehe
  Abschnitt 4) und versucht die Verbindung automatisch wiederherzustellen.

---

## 2. Inbetriebnahme

### 2.1 Erstes Einschalten

1. Panel mit 5 V versorgen. Das Display startet sofort.
2. Ohne gespeichertes WLAN startet das Panel selbst einen eigenen
   WLAN-Zugangspunkt (AP) mit dem Namen `RCT-Panel` — auch dann, wenn
   kein Netzwerk erreichbar ist.
3. Mit einem Smartphone/Laptop verbinden Sie sich mit diesem WLAN und öffnen
   die Konfigurationsseite unter `http://192.168.4.1` (ein Captive-Portal
   öffnet sich meist automatisch).

### 2.2 Konfiguration im Setup-Portal

Tragen Sie im Portal ein:

| Feld | Bedeutung | Vorgabe |
|---|---|---|
| WLAN-Name / Passwort | Ihr Heimnetzwerk | — |
| `rct_host` | IP-Adresse oder Hostname des RCT-Wechselrichters | `192.168.0.1` |
| `rct_port` | TCP-Port für das RCT-Protokoll | `8899` |

Bestätigen Sie das Formular. Das Panel speichert die Angaben dauerhaft
(NVS) und wechselt dann ins konfigurierte Netzwerk — der Zugangspunkt
`RCT-Panel` verschwindet dabei erwartungsgemäß.

> **Tipp:** Der Zugangspunkt bleibt so lange aktiv, bis das Panel erfolgreich
> konfiguriert ist. Bei falschem Passwort oder nicht erreichbarem Netzwerk
> öffnet sich das Portal automatisch wieder, damit Sie die Angaben
> korrigieren können.

<figure class="portal-shot">
  <div class="portal-shot-ph">Platzhalter: Screenshot des Webportals (Screenshot folgt)</div>
  <figcaption>Bild 2: Konfigurationsportal unter http://192.168.4.1</figcaption>
</figure>

### 2.3 Später erneut konfigurieren

- Öffnen Sie auf der Seite Service den Button „Setup starten" — das
  Panel startet daraufhin wieder den Konfigurations-Zugangspunkt.
- Oder starten Sie das Panel, während kein gespeichertes Netzwerk erreichbar
  ist (nach ca. 15 s erscheint der AP von selbst).
- Zum vollständigen Zurücksetzen auf Werkseinstellung kann der
  NVS-Speicher gelöscht werden (Entwickler-Anleitung, Abschnitt 9).

---

## 3. Bedienung

Die Bedienung erfolgt per Touch:

- ◀ / ▶ (linker/rechter Knopf unten): eine Seite zurück bzw. weiter.
- ⌂ (Home-Mitte): springt zur Übersicht.
- Die Reihenfolge der Seiten ist fest: Übersicht → Energie → Heute →
  24 h Verlauf → Info → Akku → Service (und wieder zurück).

Statusleiste (oben): links steht „RCT Power Panel", rechts der
Verbindungsstatus:

| Badge | Bedeutung |
|---|---|
| `live` (grün) | Wechselrichter verbunden, Daten aktuell |
| `connecting` (gelb) | WLAN und Verbindung werden gerade aufgebaut |
| `no data` (rot) | WLAN steht, aber es kommen keine RCT-Daten an |
| `reconnect` (gelb) | Daten kamen, der Datenstrom ist abgerissen — Neustart der Verbindung |

Zeigt eine Seite „–" statt eines Wertes, ist dieser Wert noch nicht
eingetroffen (z. B. weil der Wechselrichter keine Batterie meldet oder die
Verbindung fehlt). Es sind keine Werte ausgefallen — das Panel zeigt keinen
erfundenen Nullwert an.

---

## 4. Die sieben Seiten im Einzelnen

### 4.1 Übersicht (Energiefluss)

Das Flussdiagramm bildet die „Energiefluss"-Ansicht des RCT-Portals ab:

- PV (links): Erzeugung aus Solar-Generator A und B plus optionalem
  externen S0-Zähler.
- Haus (Mitte): aktueller Verbrauch. Hinweis: Der Wechselrichter zieht
  die externe Einspeisung bereits von seiner Lastmessung ab; das Panel
  addiert den S0-Wert wieder hinzu, sodass hier der tatsächliche
  Hausverbrauch steht.
- Netz (rechts): Bezug oder Einspeisung (negativer Wert = Einspeisung).
- Batterie (unten): Ladezustand in Prozent (im Knoten) und aktuelle
  Leistung unter dem Knoten.

Die Pfeile zwischen den Knoten leuchten rot in Richtung des aktuellen
Energieflusses. Unten zeigt eine Tabelle den Stand von Erzeugung /
Verbrauch / Netz / Batterie.

### 4.2 Energie (Balken pro Zeitraum)

Akkumulierte Energien als Balken — wählbar über die Tasten
Tag | Monat | Jahr | Gesamt:

| Balken | Farbe | Erklärung |
|---|---|---|
| PV Erzeugung | gelb | erzeugte Energie |
| Eigenverbrauch | grün | erzeugt und selbst genutzt (= PV − Einspeisung, nie negativ) |
| Netzeinspeisung | orange | eingespeiste Energie |
| Netzbezug | rot | aus dem Netz bezogene Energie |
| Verbrauch | türkis | gesamter Verbrauch |

Die Balken sind zum größten Wert des gewählten Zeitraums normiert; die
Werte stehen rechtsbündig über dem jeweiligen Balken (kWh bzw. MWh mit
Dezimalkomma).

### 4.3 Heute (Tagesübersicht)

Die Tageswerte des aktuellen Kalendertags:

- Erzeugt / Eigenverbrauch / Eingespeist (kWh),
- Verbrauch / Bezug (kWh),
- Autarkie (%): = 1 − Netzbezug ÷ Hausverbrauch des Tages
- Eigenverbrauch (%): Anteil der Erzeugung, der selbst genutzt wird.

Hinweis: Entlädt sich die Batterie zur Deckung des Hausbedarfs, zählt diese
Energie als Eigenverbrauch.

### 4.4 24 h Verlauf

Liniendiagramm der letzten 24 Stunden (ein Punkt alle 5 Minuten, 288 Punkte):

| Linie | Farbe |
|---|---|
| Netz | rot |
| Verbrauch | violett |
| PV | grün |
| EXT (externer S0-Zähler) | blau |
| Batterie | orange |
| SOC (Ladezustand) | gelb — auf eigener Achse 0–100 %: 0 % = unterer Rand, 100 % = oberer Rand |

- Die Y-Achse der Leistungswerte skaliert automatisch: Sie wächst, sobald
  ein neuer Höchstwert auftritt, und schrumpft wieder, sobald dieser aus dem
  24-Stunden-Fenster fällt. Links am Diagramm stehen Markierungen für
  Minimum, 0 und Maximum (in kW mit Dezimalkomma).
- Batterie positiv = Entladen (versorgt das Haus), negativ = Laden.
- Fehlende Daten (z. B. Gerätepause) erscheinen als Lücke in den Linien; die
  Zeile unter dem Diagramm nennt die Lückenlänge („… Lücke(n), insgesamt
  … s").
- Der Verlauf übersteht einen Neustart: Beim Hochfahren lädt das Panel
  die letzten bis zu 24 Stunden von der SD-Karte zurück.

### 4.5 Info

Technische und Verbindungsdaten (Reihenfolge wie angezeigt):

`Name` · `Software` · `RCT host` · `RCT port` · `Link` (connected/offline) ·
`Last data` (Sekunden seit letztem Datenpaket) · `Uptime` ·
`Netz L1..L3` · `PV` (A+B+S0) · `Kern` · `Kühlkörper` · `Netzfrequenz`.

### 4.6 Akku

Alles zur Batterie:

- Batterie-SOC: Ladezustand in %.
- Batterie: Leistung / Strom / Spannung. Batteriezentrisches
  Vorzeichen: Laden = „+", Entladen = „−" — also umgekehrt zum
  Flussdiagramm auf der Übersicht, wo das Entladen (Versorgung des Hauses)
  positiv ist.
- Batterie-Temp · Kalibrierung (nächster Kalibriertermin als Datum +
  Tages-Countdown, sobald die Uhrzeit synchronisiert ist) · Zyklen ·
  SOH (State of Health) · Inselbetrieb.

### 4.7 Service

Die einzige Seite mit Aktionen:

- „Setup starten" (rechts oben): öffnet das Konfigurationsportal (siehe
  Abschnitt 2.3).
- Batterie-Status: decodierter Zustand (darunter der Rohwert).
- Störungen: decodierte Fehlermeldungen des Wechselrichters (mehrere
  können gleichzeitig aktiv sein).
- SD-Log: Status der SD-Aufzeichnung, z. B. `SD: OK | 16,0 GB frei` —
  bei gezogener Karte `SD: -- | n gepuffert` (Werte werden zwischengepuffert;
  siehe Abschnitt 5).
- „Screenshot" (rechts, unter „Setup starten"): speichert nach 5
  Sekunden ein Bild des aktuellen Displays als BMP auf die Karte
  (`/shot/shot001.bmp`). Die 5 Sekunden erlauben, vorher zu einer anderen
  Seite zu wechseln. Praktisch, wenn Sie dem Support zeigen möchten, was das
  Panel anzeigt.

---

## 5. Datenerfassung auf der SD-Karte

Das Panel schreibt automatisch alle 5 Minuten einen Datensatz in eine
CSV-Datei (nur bei verbundenem Wechselrichter, keine Nullzeilen):

- Datei: `/hist/RCT-<Jahr><Monat>.csv` (z. B. `RCT-202609.csv`),
  eine Datei pro Kalendermonat. Läuft die Uhr (SNTP) beim Start noch nicht,
  schreibt das Panel zunächst in eine Uptime-Datei und wechselt nach der
  Zeitsynchronisation automatisch auf die Monatsdatei.
- Umfang: ca. 40 KB pro Tag ≈ 1,2 MB pro Monat — eine übliche Karte
  reicht jahrzehntelang.
- Karte gezogen: Solange keine Karte steckt, werden die Zeilen in einem
  RAM-Puffer (ca. 1 Stunde) zwischengelagert und nach dem Einstecken
  nachgeschrieben. Die Service-Seite zeigt den Pufferstand.

### CSV-Format (16 Spalten)

```
ts,pv_a,pv_b,s0,temp_core,temp_bat,temp_hsink,
load_l1,load_l2,load_l3,bat,soc,grid_l1,grid_l2,grid_l3,status
```

| Spalte | Bedeutung | Einheit |
|---|---|---|
| `ts` | Unix-Zeitstempel | s |
| `pv_a`, `pv_b` | PV-Leistung Generator A / B | W |
| `s0` | externer S0-Zähler | W |
| `temp_core`, `temp_bat`, `temp_hsink` | Temperaturen Kern / Batterie / Kühlkörper | °C |
| `load_l1..l3` | Verbrauch pro Phase (Haus) | W |
| `bat` | Batterieleistung (positiv = Laden) | W |
| `soc` | Ladezustand | % |
| `grid_l1..l3` | Netz-Leistung pro Phase (positiv = Bezug) | W |
| `status` | Status-/Fehlerbitmaske | — |

Die Datei ist direkt mit Tabellenkalkulationen, pandas oder Grafana
auswertbar. Leistungen und Temperaturen stehen in Watt bzw. Grad Celsius,
der Zeitstempel in Unix-Sekunden (UTC); das Panel selbst rechnet für die
Monatsdatei über die SNTP-Zeit.

---

## 6. Vorzeichen <span class="h-sub">kompakt</span>

| Größe | Konvention |
|---|---|
| Netz (Übersicht, Verlauf, Info) | `+` = Bezug, `−` = Einspeisung |
| Batterie auf Übersicht & Verlauf | `+` = Entladen (versorgt Haus), `−` = Laden |
| Batterie auf der Akku-Seite | `+` = Laden, `−` = Entladen (batteriezentrisch) |
| Batterie in der CSV (`bat`) | `+` = Laden |
| Hausverbrauch | = gemessene Last + S0 (der Wechselrichter misst die Last abzüglich der externen Einspeisung) |
| PV gesamt | = A + B + S0 |

---

## 7. Fehlerbehebung

| Symptom | Ursache / Lösung |
|---|---|
| Badge `no data` (rot) | WLAN steht, der Wechselrichter antwortet nicht. Prüfen Sie `rct_host`/`rct_port` im Setup-Portal und ob der Wechselrichter erreichbar ist. |
| Badge `connecting` bleibt | WLAN-Verbindung wird aufgebaut; wenn es nicht weitergeht, prüfen Sie das WLAN-Passwort (Portal öffnet sich nach ~15 s erneut). |
| Badge `reconnect` | Datenstrom abgerissen; das Panel versucht automatisch neu zu verbinden. |
| Kein Konfigurationsportal auffindbar | Panel ist bereits in einem Netzwerk — nutzen Sie „Setup starten" auf der Service-Seite. |
| `SD: --` auf Service-Seite | Keine Karte erkannt oder Karte gezogen; prüfen Sie die microSD im Steckplatz (FAT32). Daten werden bis ~1 h gepuffert. |
| Werte auf „–" | Wechselrichter liefert diesen Wert nicht (z. B. keine Batterie) — normal. |

---

## 8. Sicherheit

- Das Panel ist ein Anzeigegerät und greift nicht in die
  Wechselrichter-Konfiguration ein.
- Arbeiten an elektrischen Anlagen (Wechselrichter, Zählerschrank) gehören in
  Fachhände — das Panel selbst wird nur mit Kleinspannung (5 V) versorgt.
- Die angezeigten Werte dienen der Beobachtung; für abrechnungsrelevante
  Daten gilt das Portal des Herstellers.

---

## 9. Für Entwickler: Firmware aktualisieren (Kurzfassung)

Quellcode und Build liegen in diesem Repository (`rct-panel`). Voraussetzung:
PlatformIO (Core 6.x).

```sh
pio run -e esp32-s3              # bauen
pio run -e esp32-s3 -t upload    # flashen (USB)
pio device monitor               # serielle Diagnose, 115200 Baud
```

Diagnosemeldungen (z. B. `RCT: grid ...`) erscheinen im seriellen Monitor.
Zur Werkseinstellung zurück: `pio run -e esp32-s3 -t erase` (löscht
gespeichertes WLAN und RCT-Konfiguration).

> **Hinweis:** Zum Flashen verbinden Sie das Panel per USB mit dem Rechner
> und starten den Build mit Upload (siehe oben). Das Gerät startet danach
> automatisch neu; die SD-Aufzeichnung stört der Vorgang nicht.

---

*Stand: September 2026. Das Handbuch beschreibt die Firmware ab Commit
`91d4b5b` (inclusive).*