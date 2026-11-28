#pragma once
/*
 * HD_Game - Spielbetrieb: der ESP32 steuert die Anzeige anstelle des Bediengeraets.
 *
 * Enthaelt die Spieluhr (Abschnitte, Pausen), den Spielstand und die Reihenfolge der
 * Telegramme, die an die Anzeige gehen. Ohne Arduino-Abhaengigkeiten, damit sich die
 * Logik auf dem PC testen laesst (tools/test_game.cpp).
 *
 * Ablauf einer Spielform mit Pausen:
 *   - Play startet die Uhr des aktuellen Abschnitts, Pause haelt sie an.
 *   - Laeuft ein Spielabschnitt ab, ertoent das Horn 2 Sekunden lang, die Uhr zeigt dabei 0:00.
 *     Danach startet die Pause von selbst und laeuft rueckwaerts.
 *   - Laeuft die Pause ab, ertoent das Horn 1 Sekunde lang. Der naechste Abschnitt steht mit
 *     voller Zeit bereit und wartet auf Play.
 *   - Nach dem letzten Abschnitt bleibt die Uhr auf 0:00 stehen (Spielende).
 *   - Spielform mit nur einem Abschnitt: nach dem Horn steht die volle Zeit wieder bereit und
 *     wartet auf Play. Der Abschnitt bleibt 1, die Tore bleiben stehen bis HDG_Reset.
 */
#include <stdint.h>
#include <stddef.h>

#define HD_COUNT_DOWN     1      // 1: Spielzeit laeuft rueckwaerts bis 0:00    0: vorwaerts
#define HD_TX_SLOT_MS     100    // Abstand der laufenden Wiederholung (ein Telegramm je Zeitscheibe)
#define HD_TX_GAP_MS      20     // Mindestabstand zwischen zwei Telegrammen
#define HD_HORN_PERIOD_MS 2000   // Horn am Ende eines Spielabschnitts
#define HD_HORN_BREAK_MS  1000   // Horn am Ende einer Pause
#define HD_HORN_MANUAL_MS 2000   // Horn-Taste
#define HD_HORN_OFF_REPEAT 2     // "Horn aus" wird zur Sicherheit so oft wiederholt
#define HD_MAX_GOALS      99
#define HD_MAX_SECONDS    5999   // 99:59

struct HdGameConfig {
  uint8_t  periods;              // Anzahl Spielabschnitte: 1, 2 oder 4
  uint16_t periodSec;            // Laenge eines Abschnitts
  uint16_t shortBreakSec;        // Viertelpause (nach dem 1. und 3. Viertel)
  uint16_t halfBreakSec;         // Halbzeitpause
};

struct HdGame {
  bool     active;               // ESP32 steuert die Anzeige
  bool     stopping;             // Endsequenz wird noch gesendet
  bool     running;              // Uhr laeuft
  bool     inBreak;              // aktueller Abschnitt ist eine Pause
  bool     finished;             // letzter Abschnitt ist abgelaufen
  bool     hornWait;             // Abschnitt abgelaufen, Horn ertoent, danach geht es weiter
  bool     horn;                 // Horn ist an
  uint8_t  period;               // 1..periods
  int32_t  remainMs;             // Restzeit des aktuellen Abschnitts
  uint16_t home, guest;
  HdGameConfig cfg;
};

void   HDG_Init();                                  // einmal beim Start
const  HdGame &HDG_Get();
bool   HDG_SetConfig(const HdGameConfig &cfg);      // wird beim naechsten HDG_Reset wirksam
void   HDG_Start(uint32_t nowMs);                   // Anzeige uebernehmen: Startsequenz senden
void   HDG_Stop(uint32_t nowMs);                    // Endsequenz senden, Anzeige zeigt wieder die Uhrzeit
void   HDG_Reset(uint32_t nowMs);                   // neues Spiel: 0:0, 1. Abschnitt, volle Zeit, Uhr steht
void   HDG_Play(uint32_t nowMs);
void   HDG_Pause(uint32_t nowMs);
void   HDG_AdjustTime(int deltaSec, uint32_t nowMs);
void   HDG_Goal(bool home, int delta);
void   HDG_Horn(uint32_t durationMs, uint32_t nowMs); // Horn fuer die angegebene Dauer einschalten
void   HDG_Tick(uint32_t nowMs);                    // regelmaessig aufrufen

// Naechstes zu sendendes Telegramm. Rueckgabe: Laenge, 0 wenn gerade nichts zu senden ist
size_t HDG_NextFrame(uint32_t nowMs, uint8_t *out, size_t max);

// Texte so, wie sie an die Anzeige gehen
uint16_t HDG_ShownSeconds();
void   HDG_TimeText(char out[5]);                   // "MMSS"
void   HDG_ScoreText(char out[7]);                  // Heim linksbuendig, Gast rechtsbuendig
uint16_t HDG_BreakAfter(uint8_t period);            // Pausenlaenge nach einem Abschnitt in Sekunden
