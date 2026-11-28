# HockeyDisplay

Firmware für ein Waveshare **ESP32-S3-POE-ETH-8DI-8RO**, das an der RS485-Leitung der Spielstandanzeige in der HTHC-Hockeyhalle hängt. Der ESP32 liest die Telegramme zwischen Bediengerät und Anzeige mit, zeigt sie im Browser, kann die Uhr der Anzeige stellen und die Anzeige im Spielbetrieb anstelle des Bediengeräts steuern.

Diese Datei hält den gesamten Wissensstand fest: Aufbau, Bedienung, das per Reverse Engineering ermittelte Protokoll und die offenen Fragen. **Sie wird mit jeder Änderung am Projekt mitgepflegt** (siehe [Änderungsverlauf](#änderungsverlauf)).

## Inhalt

- [Funktionen](#funktionen)
- [Hardware](#hardware)
- [Einrichten und Kompilieren](#einrichten-und-kompilieren)
- [Netzwerk und Firmware-Update](#netzwerk-und-firmware-update)
- [Webseiten und API](#webseiten-und-api)
- [Spielbetrieb](#spielbetrieb)
- [RS485-Protokoll](#rs485-protokoll)
- [Aufbau des Codes](#aufbau-des-codes)
- [Offene Punkte](#offene-punkte)
- [Änderungsverlauf](#änderungsverlauf)

## Funktionen

| Funktion | Stand |
| --- | --- |
| RS485 mithören und Telegramme dekodieren | fertig, auf dem PC gegen die Mitschnitte getestet |
| Rohdaten im Browser anzeigen (Monitor) | fertig |
| Anzeige im Browser nachbauen | fertig, Anordnung noch nicht mit der echten Anzeige abgeglichen |
| Uhr der Anzeige über die Webseite stellen | fertig, an der echten Anzeige noch nicht erprobt |
| Firmware-Update über das Netzwerk (OTA) | fertig |
| Spielbetrieb über die Webseite: Tore, Spielzeit, Viertel, Pausen, Horn | fertig, an der echten Anzeige noch nicht erprobt |
| Countdown bis zum Anpfiff, Setup-Seite für alle Zeiten | geplant |

## Hardware

- Board: Waveshare ESP32-S3-POE-ETH-8DI-8RO (ESP32-S3, 16 MB Flash, PSRAM, Ethernet mit PoE, RS485, 8 Eingänge, 8 Relais)
- RS485 liegt an UART1: TXD = GPIO 17, RXD = GPIO 18 (`WS_GPIO.h`)
- Schnittstelle: **19200 Baud, 8N1**. Die Baudrate steht fest im Code (`HD_BAUD` in `WS_RS485.h`)
- Der ESP32 hängt parallel zu Bediengerät und Anzeige am Bus

## Einrichten und Kompilieren

Arduino IDE mit dem Boardpaket **esp32 by Espressif Systems 4.0.0**.

| Einstellung | Wert |
| --- | --- |
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB |
| Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |
| PSRAM | OPI PSRAM |

Das Partitionsschema muss zwei App-Partitionen haben, sonst funktioniert das Update über das Netzwerk nicht.

Benötigte Bibliotheken im Arduino-Bibliotheksordner: **ArduinoJson**, **PubSubClient**, **NTPClient**. Kopien liegen zur Dokumentation im Ordner `libraries/`. Die Arduino IDE verwendet diesen Ordner nicht.

### Zugangsdaten

WLAN-Zugangsdaten und OTA-Passwort stehen in `secrets.h`. Die Datei wird nicht zu GitHub hochgeladen (`.gitignore`). Einmalig anlegen:

```bash
cp secrets.example.h secrets.h
```

Danach WLAN-Name, WLAN-Passwort und OTA-Passwort eintragen.

### Arbeitsablauf mit Git

```bash
git pull                 # Stand von GitHub holen
git add -A
git commit -m "Beschreibung der Änderung"
git push                 # eigenen Stand hochladen
```

## Netzwerk und Firmware-Update

- WLAN mit fester Adresse **192.168.1.16**, Gateway und DNS 192.168.1.1, Netzmaske 255.255.255.0 (`WS_Information.h`). Mit `HD_USE_STATIC_IP 0` bezieht der ESP32 die Adresse per DHCP.
- Hostname: `hockeydisplay`
- Ethernet ist aus der Waveshare-Demo weiterhin aktiv.
- **OTA:** Nach dem ersten Upload per USB erscheint in der Arduino IDE unter „Werkzeuge → Port“ der Netzwerk-Port `hockeydisplay`. Beim Upload fragt die IDE nach dem OTA-Passwort aus `secrets.h`.

## Webseiten und API

| Adresse | Inhalt |
| --- | --- |
| `/` | Nachbau der Anzeige (Tore, Spielzeit, Viertel, Horn, Feld 0x26) und darunter die Bedienung des [Spielbetriebs](#spielbetrieb). Außerhalb des Spielmodus zeigt die Seite die Uhrzeit des Browsers. |
| `/monitor` | Jedes Telegramm mit Zeitstempel, Abstand zum vorigen, Hex, ASCII und Klartext. Filter „nur Änderungen“ und „nur unbekannte“, Speichern als Textdatei. Vom ESP32 gesendete Telegramme sind blau. |
| `/uhr` | Uhr der Anzeige stellen: zum nächsten Minutenwechsel mit der Browserzeit oder sofort mit einer eingetragenen Zeit. |
| `/relay` | Relais-Seite der Waveshare-Demo |

| API | Bedeutung |
| --- | --- |
| `GET /api/state` | Zustand als JSON (Spielmodus, Zeit, Tore, Viertel, Horn, Zähler, Baudrate) |
| `GET /api/log?since=<Nr>` | Telegramme ab der laufenden Nummer als JSON |
| `GET /api/clock?d=TTMMJJHHMM` | Uhr der Anzeige stellen. **Sendet auf dem RS485-Bus.** |
| `GET /api/game?cmd=...` | Spielbetrieb, siehe unten. **Sendet auf dem RS485-Bus.** |

Kommandos von `/api/game`:

| `cmd` | Parameter | Wirkung |
| --- | --- | --- |
| `start` | `p`, `min`, `sb`, `hb` | Spielbetrieb starten, neues Spiel mit dieser Spielform |
| `stop` | | Spielbetrieb beenden, Anzeige zeigt wieder die Uhrzeit |
| `play`, `pause` | | Uhr starten und anhalten |
| `reset` | `p`, `min`, `sb`, `hb` | Neues Spiel: 0:0, erstes Viertel, volle Zeit, Uhr steht |
| `goal` | `team=h` oder `g`, `d=1` oder `-1` | Tor zählen oder zurücknehmen |
| `time` | `d` in Sekunden, −600 bis 600 | Spielzeit korrigieren |
| `horn` | | Horn für 2 Sekunden |

`p` ist die Zahl der Viertel (1, 2 oder 4), `min` die Minuten je Viertel, `sb` die Viertelpause und `hb` die Halbzeitpause in Minuten. Die Antwort ist der Zustand wie bei `/api/state`.

Die Webseiten haben keinen Zugriffsschutz. Jeder im WLAN kann sie öffnen, die Uhr stellen und den Spielbetrieb bedienen.

### Wann der ESP32 sendet

Ohne Bedienung hört der ESP32 nur mit. Er sendet in zwei Fällen:

1. **Uhr stellen:** ein einzelnes Telegramm. Nur wenn in den letzten 3 Sekunden kein Spieltelegramm vom Bediengerät kam (`HD_TIMEOUT_MS`), der Spielbetrieb über die Webseite nicht läuft und der Bus mindestens 100 ms ruhig war (`HD_BUS_IDLE_MS`).
2. **Spielbetrieb:** laufend, solange er über die Webseite gestartet ist. Der Start wird abgelehnt, solange das Bediengerät Spieltelegramme sendet.

**Bediengerät und ESP32 dürfen nicht gleichzeitig im Spielbetrieb senden.** Der ESP32 erkennt ein Bediengerät nur beim Start. Wird es später eingeschaltet, stören sich beide auf dem Bus.

## Spielbetrieb

Der ESP32 übernimmt die Rolle des Bediengeräts. Spieluhr und Spielstand laufen auf dem ESP32, nicht im Browser: Die Anzeige läuft weiter, wenn die Seite geschlossen wird, und mehrere Geräte können gleichzeitig bedienen.

### Bedienung auf der Seite `/`

| Element | Wirkung |
| --- | --- |
| **Spielbetrieb starten** | Sendet die Startsequenz und beginnt ein neues Spiel mit der gewählten Spielform. Die Uhr steht. |
| **+** und **−** je Mannschaft | Tor zählen, Tor zurücknehmen (0 bis 99). GAST steht links, HEIM rechts. |
| **Play**, **Pause** | Uhr starten und anhalten. Die vier großen Tasten für GAST, Play, Pause und HEIM sind gleich groß. |
| **Horn** | Horn für 2 Sekunden |
| **Spielzeit korrigieren** | ± 1 Minute, ± 10 Sekunden, ± 1 Sekunde, auch bei laufender Uhr |
| **Nächstes Spiel** | Spielform und Minuten für das nächste Spiel |
| **Neues Spiel** | 0:0, erstes Viertel, volle Zeit, Uhr steht. Löst erst beim zweiten Tippen aus. |
| **Spielbetrieb beenden** | Sendet die Endsequenz, die Anzeige zeigt wieder die Uhrzeit. Löst erst beim zweiten Tippen aus. |

Die Seiten von GAST und HEIM lassen sich in `HD_Web.cpp` mit der Konstante `LEFT` tauschen.

### Spielformen

| Auswahl | Viertel | Minuten | Pausen |
| --- | --- | --- | --- |
| U8 | 1 | 10 voreingestellt, 8 bis 15 üblich | keine |
| U12 | 2 | 10 | 2 min |
| U12 | 2 | 12 | 2 min |
| U14 bis U18 | 2 | 15 | 5 min |
| Bundesliga | 4 | 15 | 1 min, 10 min, 1 min |
| Turnier / Testspiel | 1 | 15 voreingestellt | keine |

Die Minuten je Viertel lassen sich bei jeder Spielform im Feld daneben ändern (1 bis 99). Die Spielformen stehen in `HD_Web.cpp` in der Liste `FORMS`.

### Ablauf

- Die Spielzeit läuft rückwärts bis 0:00 (`HD_COUNT_DOWN` in `HD_Game.h`).
- Läuft ein Viertel ab, ertönt das Horn 2 Sekunden lang. Die Uhr zeigt dabei 0:00, Play und Zeitkorrektur sind so lange gesperrt.
- Danach startet die Pause von selbst und läuft im Zeitfeld rückwärts. Die Webseite zeigt über der Zeit „PAUSE“. Die Viertel-Anzeige bleibt während der Pause auf dem abgelaufenen Viertel stehen und wechselt erst am Pausenende.
- Läuft die Pause ab, ertönt das Horn 1 Sekunde lang. Das nächste Viertel steht mit voller Zeit bereit und wartet auf **Play**.
- Nach dem letzten Viertel ertönt das Horn 2 Sekunden lang. Die Uhr bleibt auf 0:00 und die Tore bleiben stehen, auch bei Spielformen mit nur einem Viertel. Erst **Neues Spiel** setzt die Zeit auf die volle Spielzeit und die Tore auf null. Mit einer Zeitkorrektur nach oben lässt sich weiterspielen.

Die Horn-Zeiten stehen in `HD_Game.h` (`HD_HORN_PERIOD_MS`, `HD_HORN_BREAK_MS`, `HD_HORN_MANUAL_MS`).

### Horn

Das Horn wird mit `@@a` eingeschaltet und mit `@@A` ausgeschaltet, die Anzeige schaltet es nicht von selbst ab. Der ESP32 sendet „Horn aus“ deshalb dreimal im Abstand von 0,3 s. „Spielbetrieb beenden“ schaltet das Horn ebenfalls aus. **Startet der ESP32 neu, während das Horn läuft, bleibt es an**, bis der Spielbetrieb wieder gestartet oder das Bediengerät benutzt wird.

### Gesendete Telegramme

- **Start:** dieselbe Folge wie beim Bediengerät, einschließlich der vier langen Telegramme. Deren Bedeutung ist unbekannt, sie werden unverändert wiederholt.
- **Laufend:** alle 100 ms ein Telegramm in der Reihenfolge des Mitschnitts: HEIM, GAST, Zeit, HEIM oder GAST im Wechsel, vier Blöcke von Feld 0x26 mit `---`. Änderungen von Horn, Zeit, Toren und Viertel gehen sofort raus. Der Abstand von 100 ms ist angenommen, das Bediengerät wurde ohne Zeitstempel mitgeschnitten (`HD_TX_SLOT_MS`).
- **Ende:** dieselbe Folge wie beim Bediengerät.

## RS485-Protokoll

Ermittelt aus Mitschnitten zwischen Bediengerät und Anzeige. Alle Mitschnitte liegen in `tools/mitschnitt.txt` und dienen als Testdaten für den Dekoder.

### Kurze Telegramme

```
02 3F 3F 52 44 <Feld> <Pos> <Text ...> 03 <BCC>
STX '?' '?' 'R' 'D'                    ETX
```

- `3F 3F` („??“) ist vermutlich die Adresse (Rundruf), `52 44` („RD“) der Befehl.
- **Feld** wählt den Bereich der Anzeige, **Pos** die Startposition im Feld. Beide Werte haben den Offset 0x20.
- **Text** besteht aus ASCII-Zeichen. Leerzeichen löschen eine Stelle.

### Prüfsumme (BCC)

XOR über alle Bytes nach dem `02` bis einschließlich `03`. Es ist kein CRC-8.

**Sonderregel:** Ergibt das XOR einen Wert von 0x00 bis 0x04, steht im Telegramm stattdessen `20`. Vermutlich soll die Prüfsumme nicht mit Steuerzeichen wie STX und ETX verwechselt werden. Die Regel ist an 299 Telegrammen bestätigt.

### Felder

| Feld | Pos | Text | Bedeutung | Beispiel |
| --- | --- | --- | --- | --- |
| 0x20 | `%` (0x25) | `0` | Spielmodus EIN (Startsequenz) | `02 3F 3F 52 44 20 25 30 03 20` |
| 0x20 | `!` (0x21) | `0` | Spielmodus AUS, zurück zur Uhrzeit | `02 3F 3F 52 44 20 21 30 03 24` |
| 0x20 | `0` (0x30) | `TTMMJJHHMM` | **Datum und Uhrzeit stellen** | siehe unten |
| 0x21 | 0x20 | 4 Zeichen `MMSS` | Spielzeit, führende Stellen als Leerzeichen | ` 020` = 0:20 |
| 0x22 | 0x20 | 3 Zeichen | Status und Horn | `@@a` Horn EIN, `@@A` Horn AUS, `@@@` nach Spielende |
| 0x23 | 0x20 | 3 Zeichen, linksbündig | Tore HEIM | `0  `, `17 ` |
| 0x23 | 0x23 | 3 Zeichen, rechtsbündig | Tore GAST | `  0`, ` 17` |
| 0x25 | 0x20 | 1 Zeichen | Viertel 1 bis 4 | `1` |
| 0x26 | 0x21, 0x25, 0x29, 0x2D | je 3 Zeichen | vier Blöcke, vermutlich Strafzeiten | `---` |

Bei Feld 0x20 ist das Byte nach dem Feld keine Position, sondern eine Befehlskennung.

Beim Horn unterscheidet Bit 5 des dritten Zeichens zwischen EIN (`a` = 0x61) und AUS (`A` = 0x41).

### Datum und Uhrzeit stellen

```
02 3F 3F 52 44 20 30 32 38 31 31 32 36 30 39 32 33 03 20
                     |  2  8  1  1  2  6  0  9  2  3
                     '0'  Tag  Monat Jahr Std.  Min.
```

Das Beispiel stellt die Uhr auf den 28.11.26, 09:23 Uhr. Das Telegramm enthält keine Sekunden. Die Prüfsumme ergibt hier 0x03 und wird deshalb als `20` gesendet.

### Lange Telegramme

```
02 <Länge High> <Länge Low> <Nutzdaten ...> 03 <BCC>
```

Die Länge zählt alle Bytes nach dem Längenfeld, einschließlich `03` und BCC. Die Nutzdaten dürfen `02` und `03` enthalten. Die Prüfsumme wird wie bei den kurzen Telegrammen gebildet. Die Bedeutung ist unbekannt.

| Telegramm | Nutzdaten | Vorkommen |
| --- | --- | --- |
| `02 00 08 3F 3F 53 4C 4B 32 03 6D` | `??SLK2` | Start- und Endsequenz |
| `02 00 08 3F 3F 52 4E 02 00 03 15` | `??RN` + `02 00` | Start- und Endsequenz |
| `02 00 0E 3F 3F 52 30 01 00 01 00 00 01 00 00 03 6E` | `??R0` + 8 Bytes | Start- und Endsequenz |
| `02 00 0E 3F 3F 52 30 01 00 02 01 00 01 00 00 03 6C` | `??R0` + 8 Bytes | Start- und Endsequenz |
| `02 00 06 3F 3F 54 30 03 61` | `??T0` | nur Endsequenz |

### Abläufe

- **Start des Spielmodus:** Spielmodus EIN, Status `@@A`, Spielzeit, Viertel, die vier Blöcke von Feld 0x26 (leer), Tore HEIM, die vier langen Telegramme, Tore GAST.
- **Im Spiel:** Das Bediengerät wiederholt laufend Tore HEIM, Tore GAST, Spielzeit und die vier Blöcke von Feld 0x26.
- **Ende:** Spielmodus AUS, `??T0`, Status `@@@`, danach werden alle Felder mit Leerzeichen gelöscht. Zwischen Tore HEIM und Tore GAST stehen wieder die vier langen Telegramme.
- **Idle:** Ein eigenes Idle-Telegramm wurde nicht gefunden. Vermutlich hält die laufende Wiederholung die Anzeige im Spielmodus. Bleibt sie aus, schaltet die Anzeige auf die Uhrzeit zurück.

## Aufbau des Codes

| Datei | Inhalt |
| --- | --- |
| `HockeyDisplay.ino` | Start, ruft die Module der Waveshare-Demo auf |
| `HD_Protocol.h/.cpp` | Dekoder, Zustand der Anzeige, Telegramm-Log, Aufbau von Sendetelegrammen. Ohne Arduino-Abhängigkeiten. |
| `HD_Game.h/.cpp` | Spielbetrieb: Spieluhr, Viertel und Pausen, Spielstand, Reihenfolge der gesendeten Telegramme. Ohne Arduino-Abhängigkeiten. |
| `HD_Web.h/.cpp` | Webseiten und API |
| `WS_RS485.h/.cpp` | UART, Task für Mithören und Senden |
| `WS_WIFI.cpp` | WLAN, feste IP, Webserver, OTA, Relais-Seite der Demo |
| `WS_Information.h` | Netzwerk-Einstellungen, MQTT-Daten der Demo |
| `secrets.example.h` | Vorlage für `secrets.h` |
| `tools/` | Mitschnitte und Host-Tests, wird von der Arduino IDE nicht kompiliert |
| übrige `WS_*`-Dateien | unveränderte Waveshare-Demo (Relais, Eingänge, RTC, Ethernet, MQTT, CAN, SD) |

Das Bluetooth-Modul der Demo wurde entfernt, weil seine Schnittstelle im Boardpaket 4.0.0 nicht mehr existiert.

### Auf dem PC testen

```bash
g++ -std=c++17 -I. tools/test_protocol.cpp HD_Protocol.cpp -o /tmp/hdtest
/tmp/hdtest tools/mitschnitt.txt

g++ -std=c++17 -I. tools/test_game.cpp HD_Game.cpp HD_Protocol.cpp -o /tmp/hdgame
/tmp/hdgame
```

Der erste Test prüft alle Mitschnitte auf gültige Prüfsummen und vergleicht das erzeugte Uhr-Telegramm Byte für Byte mit dem mitgeschnittenen. Der zweite spielt die Spielformen im Zeitraffer durch und schickt jedes gesendete Telegramm durch den Dekoder.

## Offene Punkte

- **Uhr stellen an der echten Anzeige erproben:** ob die Anzeige das Telegramm ohne vorherige Startsequenz annimmt und ob sie die Sekunden dabei auf null setzt.
- **Idle-Verhalten bestätigen:** Abstände der Wiederholungen im Monitor ablesen und prüfen, nach welcher Pause die Anzeige zurückschaltet.
- **Lange Telegramme** (`SLK2`, `RN`, `R0`, `T0`) deuten.
- **Feld 0x26** bestätigen (Strafzeiten?) und die Bedeutung der freien Positionen 0, 4, 8 und 12 klären.
- **Anordnung** der Browser-Anzeige mit einem Foto der echten Anzeige abgleichen.
- **Spielbetrieb an der echten Anzeige erproben:** ob sie die Startsequenz des ESP32 annimmt, im Spielmodus bleibt und nach dem Beenden zur Uhrzeit zurückkehrt.
- **Abstand der Wiederholung** (100 ms) mit dem Bediengerät vergleichen: Abstände im Monitor ablesen.
- **Laufrichtung der Uhr:** Der Mitschnitt des Bediengeräts zählt von 0:00 aufwärts, der Spielbetrieb zählt rückwärts. Klären, was gewünscht ist.
- **Seiten von HEIM und GAST** mit der echten Anzeige abgleichen.
- **Neustart des ESP32:** Spielstand und Uhr gehen verloren, der Spielbetrieb ist danach aus. Ein laufendes Horn bleibt an.

### Geplant

- **Countdown bis zum Anpfiff** (Bundesliga): Beginnt das Spiel laut Spielplan zum Beispiel um 13:00 Uhr, läuft die Restzeit bis dahin als Countdown auf der Anzeige. 5 Minuten vor dem Anpfiff ertönt das Horn 1 Sekunde lang.
- **Setup-Seite:** Alle Zeiten (Spielzeiten, Pausen, Horn-Dauern, Anpfiff-Countdown) sollen sich dort einstellen lassen statt im Code.
- **WLAN-Passwort:** Es stand in früheren Versionen von `WS_Information.h` und ist in der Git-Historie weiterhin lesbar. Passwort ändern oder das Repository auf privat stellen.
- Die Waveshare-Demo verbindet sich weiterhin per MQTT mit `mqtt.waveshare.cloud`. Prüfen, ob das gewünscht ist.

## Änderungsverlauf

| Datum | Änderung |
| --- | --- |
| 21.11.2026 | Waveshare-Demo für das Board als Ausgangsstand hochgeladen |
| 21.11.2026 | Bluetooth-Modul entfernt (Boardpaket 4.0.0) |
| 21.11.2026 | Dekoder für das Protokoll, RS485-Monitor, Nachbau der Anzeige, feste IP, OTA, `secrets.h` |
| 28.11.2026 | Telegramm zum Stellen von Datum und Uhrzeit ermittelt und dokumentiert, Seite `/uhr` und `/api/clock`, README angelegt |
| 28.11.2026 | Baudrate fest auf 19200 gesetzt, Umstellung im Monitor und `/api/baud` entfernt |
| 28.11.2026 | Spielbetrieb: Bedienung auf der Anzeigeseite (Tore, Play/Pause, Zeitkorrektur, Spielformen, Neues Spiel), `HD_Game`, `/api/game`. GAST steht auf der Seite jetzt links, HEIM rechts. |
| 28.11.2026 | Horn: 2 s am Ende eines Viertels, 1 s am Ende einer Pause, Horn-Taste (2 s). Anzeige „PAUSE“ auf der Webseite. Tasten GAST, Play, Pause und HEIM gleich groß. |
