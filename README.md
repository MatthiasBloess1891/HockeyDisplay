# HockeyDisplay

Firmware für ein Waveshare **ESP32-S3-POE-ETH-8DI-8RO**, das an der RS485-Leitung der Spielstandanzeige in der HTHC-Hockeyhalle hängt. Der ESP32 liest die Telegramme zwischen Bediengerät und Anzeige mit, zeigt sie im Browser und kann die Uhr der Anzeige stellen.

Diese Datei hält den gesamten Wissensstand fest: Aufbau, Bedienung, das per Reverse Engineering ermittelte Protokoll und die offenen Fragen. **Sie wird mit jeder Änderung am Projekt mitgepflegt** (siehe [Änderungsverlauf](#änderungsverlauf)).

## Inhalt

- [Funktionen](#funktionen)
- [Hardware](#hardware)
- [Einrichten und Kompilieren](#einrichten-und-kompilieren)
- [Netzwerk und Firmware-Update](#netzwerk-und-firmware-update)
- [Webseiten und API](#webseiten-und-api)
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
| Spielstand, Zeit und Viertel vom ESP32 aus setzen | offen |

## Hardware

- Board: Waveshare ESP32-S3-POE-ETH-8DI-8RO (ESP32-S3, 16 MB Flash, PSRAM, Ethernet mit PoE, RS485, 8 Eingänge, 8 Relais)
- RS485 liegt an UART1: TXD = GPIO 17, RXD = GPIO 18 (`WS_GPIO.h`)
- Schnittstelle: 8N1. Die Baudrate ist auf der Monitor-Seite einstellbar und wird gespeichert, Startwert 9600 (`HD_DEFAULT_BAUD` in `WS_RS485.h`)
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
| `/` | Nachbau der Anzeige: Tore, Spielzeit, Viertel, Horn, Feld 0x26. Außerhalb des Spielmodus zeigt die Seite die Uhrzeit des Browsers. |
| `/monitor` | Jedes Telegramm mit Zeitstempel, Abstand zum vorigen, Hex, ASCII und Klartext. Filter „nur Änderungen“ und „nur unbekannte“, Speichern als Textdatei, Baudrate umstellen. Vom ESP32 gesendete Telegramme sind blau. |
| `/uhr` | Uhr der Anzeige stellen: zum nächsten Minutenwechsel mit der Browserzeit oder sofort mit einer eingetragenen Zeit. |
| `/relay` | Relais-Seite der Waveshare-Demo |

| API | Bedeutung |
| --- | --- |
| `GET /api/state` | Zustand als JSON (Spielmodus, Zeit, Tore, Viertel, Horn, Zähler, Baudrate) |
| `GET /api/log?since=<Nr>` | Telegramme ab der laufenden Nummer als JSON |
| `GET /api/baud?value=<Baud>` | Baudrate umstellen und speichern |
| `GET /api/clock?d=TTMMJJHHMM` | Uhr der Anzeige stellen. **Sendet auf dem RS485-Bus.** |

Die Webseiten haben keinen Zugriffsschutz. Jeder im WLAN kann sie öffnen und die Uhr stellen.

### Wann der ESP32 sendet

Der ESP32 sendet ausschließlich das Telegramm zum Stellen der Uhr, und nur wenn beide Bedingungen erfüllt sind:

1. In den letzten 3 Sekunden kam kein Spieltelegramm vom Bediengerät (`HD_TIMEOUT_MS`).
2. Der Bus war mindestens 100 ms ruhig (`HD_BUS_IDLE_MS`). Darauf wartet der ESP32 bis zu 0,5 s.

Sonst lehnt die API mit einer Fehlermeldung ab.

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
| `HD_Web.h/.cpp` | Webseiten und API |
| `WS_RS485.h/.cpp` | UART, Mithör-Task, Baudrate, Senden des Uhr-Telegramms |
| `WS_WIFI.cpp` | WLAN, feste IP, Webserver, OTA, Relais-Seite der Demo |
| `WS_Information.h` | Netzwerk-Einstellungen, MQTT-Daten der Demo |
| `secrets.example.h` | Vorlage für `secrets.h` |
| `tools/` | Mitschnitte und Host-Test, wird von der Arduino IDE nicht kompiliert |
| übrige `WS_*`-Dateien | unveränderte Waveshare-Demo (Relais, Eingänge, RTC, Ethernet, MQTT, CAN, SD) |

Das Bluetooth-Modul der Demo wurde entfernt, weil seine Schnittstelle im Boardpaket 4.0.0 nicht mehr existiert.

### Dekoder auf dem PC testen

```bash
g++ -std=c++17 -I. tools/test_protocol.cpp HD_Protocol.cpp -o /tmp/hdtest
/tmp/hdtest tools/mitschnitt.txt
```

Der Test prüft alle Mitschnitte auf gültige Prüfsummen und vergleicht das erzeugte Uhr-Telegramm Byte für Byte mit dem mitgeschnittenen.

## Offene Punkte

- **Uhr stellen an der echten Anzeige erproben:** ob die Anzeige das Telegramm ohne vorherige Startsequenz annimmt und ob sie die Sekunden dabei auf null setzt.
- **Baudrate** der Anzeige hier eintragen, sobald sie bestätigt ist.
- **Idle-Verhalten bestätigen:** Abstände der Wiederholungen im Monitor ablesen und prüfen, nach welcher Pause die Anzeige zurückschaltet.
- **Lange Telegramme** (`SLK2`, `RN`, `R0`, `T0`) deuten.
- **Feld 0x26** bestätigen (Strafzeiten?) und die Bedeutung der freien Positionen 0, 4, 8 und 12 klären.
- **Anordnung** der Browser-Anzeige mit einem Foto der echten Anzeige abgleichen.
- **Senden von Spielstand, Zeit und Viertel** vom ESP32 aus, als Ersatz für das Bediengerät.
- **WLAN-Passwort:** Es stand in früheren Versionen von `WS_Information.h` und ist in der Git-Historie weiterhin lesbar. Passwort ändern oder das Repository auf privat stellen.
- Die Waveshare-Demo verbindet sich weiterhin per MQTT mit `mqtt.waveshare.cloud`. Prüfen, ob das gewünscht ist.

## Änderungsverlauf

| Datum | Änderung |
| --- | --- |
| 21.11.2026 | Waveshare-Demo für das Board als Ausgangsstand hochgeladen |
| 21.11.2026 | Bluetooth-Modul entfernt (Boardpaket 4.0.0) |
| 21.11.2026 | Dekoder für das Protokoll, RS485-Monitor, Nachbau der Anzeige, feste IP, OTA, `secrets.h` |
| 28.11.2026 | Telegramm zum Stellen von Datum und Uhrzeit ermittelt und dokumentiert, Seite `/uhr` und `/api/clock`, README angelegt |
