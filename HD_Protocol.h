#pragma once
/*
 * HD_Protocol - Dekoder fuer das RS485-Protokoll der Spielstandanzeige (HTHC Halle).
 *
 * Bewusst ohne Arduino-Abhaengigkeiten geschrieben, damit sich der Dekoder auch auf dem
 * PC gegen Mitschnitte testen laesst (siehe tools/test_protocol.cpp).
 *
 * Telegrammformate (aus dem Reverse Engineering):
 *
 *   Kurz:  02 3F 3F 52 44 <Feld> <Pos> <Text...> 03 <BCC>
 *          STX '?' '?' 'R' 'D'  Feld und Pos jeweils mit Offset 0x20, Text in ASCII
 *
 *   Lang:  02 <LenH> <LenL> <Nutzdaten...> 03 <BCC>
 *          Laenge = Anzahl der Bytes nach dem Laengenfeld (inkl. ETX und BCC)
 *
 *   BCC:   XOR ueber alle Bytes nach STX bis einschliesslich ETX.
 *          Ergibt das XOR einen Wert von 0x00 bis 0x04, steht im Telegramm 0x20.
 *
 * Die vollstaendige Beschreibung des Protokolls steht in README.md.
 */
#include <stdint.h>
#include <stddef.h>

#define HD_FRAME_MAX   48      // maximale Telegrammlaenge im Protokoll-Log
#define HD_LOG_SIZE    256     // Anzahl Telegramme im Ringpuffer
#define HD_GAP_MS      50      // Pause, nach der ein unvollstaendiges Telegramm verworfen wird
#define HD_TIMEOUT_MS  3000    // ohne gueltiges Telegramm gilt das Bediengeraet als getrennt

// Flags eines mitgeschnittenen Telegramms
#define HD_F_CHK_OK      0x01  // Pruefsumme stimmt
#define HD_F_LONG        0x02  // Langes Telegramm (mit Laengenfeld)
#define HD_F_INCOMPLETE  0x04  // Telegramm abgebrochen
#define HD_F_NOISE       0x08  // Bytes ausserhalb eines Telegramms
#define HD_F_CHK_SUBST   0x10  // Pruefsumme war 0x00..0x04 und wurde als 0x20 gesendet
#define HD_F_KNOWN       0x20  // Bedeutung des Telegramms ist bekannt
#define HD_F_TX          0x40  // vom ESP32 selbst gesendet

struct HdFrame {
  uint32_t seq;                // laufende Nummer
  uint32_t ms;                 // Zeitstempel (millis)
  uint8_t  len;
  uint8_t  flags;
  uint8_t  data[HD_FRAME_MAX];
};

// Abbild der Anzeige, so wie es die Telegramme beschreiben (Textfelder, mit Leerzeichen gefuellt)
struct HdState {
  char     time[5];            // Feld 0x21: 4 Zeichen MMSS, z.B. " 020" = 0:20
  char     status[4];          // Feld 0x22: 3 Zeichen, z.B. "@@A" / "@@a" (Horn)
  char     score[7];           // Feld 0x23: Pos 0..2 Heim (linksbuendig), Pos 3..5 Gast (rechtsbuendig)
  char     period[2];          // Feld 0x25: Viertel
  char     pen[17];            // Feld 0x26: 4 Bloecke (Pos 1, 5, 9, 13), vermutlich Strafzeiten
  char     mode;               // Feld 0x20: '%' beim Start, '!' beim Beenden des Spielmodus
  bool     closed;             // Spielmodus wurde vom Bediengeraet beendet
  bool     seen;               // es wurde schon mindestens ein gueltiges Telegramm empfangen
  uint32_t lastValidMs;        // Zeitpunkt des letzten gueltigen Telegramms
  bool     gameSeen;           // es wurde schon ein Spieltelegramm empfangen
  uint32_t lastGameMs;         // Zeitpunkt des letzten Spieltelegramms (Felder 0x21..0x26, Start)
  uint32_t frames;             // Zaehler gueltige Telegramme
  uint32_t chkErrors;          // Zaehler Pruefsummenfehler
  uint32_t incomplete;         // Zaehler abgebrochene Telegramme / Stoerbytes
};

void     HD_Reset();                                   // Dekoder und Zustand zuruecksetzen
void     HD_Feed(uint8_t b, uint32_t nowMs);           // ein empfangenes Byte verarbeiten
void     HD_Tick(uint32_t nowMs);                      // regelmaessig aufrufen (Timeout-Behandlung)
const HdState &HD_GetState();
bool     HD_Online(uint32_t nowMs);                    // Bediengeraet sendet Spieltelegramme
bool     HD_GameMode(uint32_t nowMs);                  // Anzeige ist im Spielmodus

uint32_t HD_LogHead();                                 // Nummer des naechsten Telegramms
bool     HD_LogGet(uint32_t seq, HdFrame *out);        // Telegramm aus dem Ringpuffer lesen
void     HD_LogClear();

uint8_t  HD_Bcc(const uint8_t *p, size_t n);           // XOR ueber n Bytes
size_t   HD_Describe(const HdFrame &f, char *out, size_t n);   // Klartext zu einem Telegramm

// Telegramme zum Senden aufbauen. Rueckgabe: Laenge, 0 bei ungueltigen Werten
size_t   HD_BuildShort(uint8_t field, uint8_t pos, const char *text, uint8_t *out, size_t max);
size_t   HD_BuildClock(int day, int month, int year2, int hour, int minute, uint8_t *out, size_t max);
void     HD_LogSent(const uint8_t *data, size_t len, uint32_t nowMs);  // gesendetes Telegramm ins Log

void     HD_FormatTime(const char *raw4, char *out, size_t n); // " 020" -> "0:20"
