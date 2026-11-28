// Host-Test fuer den Spielbetrieb. Aufruf (im Sketch-Ordner):
//   g++ -std=c++17 -I. tools/test_game.cpp HD_Game.cpp HD_Protocol.cpp -o /tmp/hdgame && /tmp/hdgame
// Prueft den Ablauf der Spielformen und schickt jedes erzeugte Telegramm durch den Dekoder.
#include "HD_Game.h"
#include "HD_Protocol.h"
#include <cstdio>
#include <cstring>
#include <string>

static int fails = 0;
static uint32_t now = 1000;
static int txFrames = 0;
#define CHECK(c) do { if (!(c)) { printf("FEHLER Zeile %d: %s\n", __LINE__, #c); fails++; } } while (0)

// Zeit vergehen lassen und dabei alle Telegramme wie auf dem Bus durch den Dekoder schicken
static void run(uint32_t ms) {
  uint32_t end = now + ms;
  uint8_t f[HD_FRAME_MAX];
  while (now < end) {
    now += 5;
    HDG_Tick(now);
    size_t n = HDG_NextFrame(now, f, sizeof(f));
    if (n) { txFrames++; for (size_t i = 0; i < n; i++) HD_Feed(f[i], now); }
    HD_Tick(now);
  }
}
static std::string timeText() { char t[5]; HDG_TimeText(t); return t; }

int main() {
  HD_Reset();
  HDG_Init();

  // Bundesliga: 4 x 15 Minuten, Pausen 1 / 10 / 1 Minute
  HdGameConfig bl = { 4, 15 * 60, 60, 10 * 60 };
  CHECK(HDG_SetConfig(bl));
  HDG_Reset(now);
  CHECK(HDG_NextFrame(now + 500, nullptr, 0) == 0);
  run(500);
  CHECK(txFrames == 0);                                  // ohne Start wird nichts gesendet

  HDG_Start(now);
  run(1500);
  const HdState &s = HD_GetState();
  CHECK(s.chkErrors == 0 && s.incomplete == 0);
  CHECK(HD_GameMode(now));
  CHECK(std::string(s.time) == "1500");
  CHECK(std::string(s.score) == "0    0");
  CHECK(std::string(s.period) == "1");
  CHECK(std::string(s.pen) == " --- --- --- ---");
  CHECK(std::string(s.status) == "@@A");

  HDG_Play(now);
  run(10 * 1000);
  CHECK(timeText() == "1450" && std::string(s.time) == "1450");
  HDG_Pause(now);
  run(3000);
  CHECK(timeText() == "1450");                           // Uhr steht

  HDG_Goal(true, +1); HDG_Goal(true, +1); HDG_Goal(false, +1); HDG_Goal(true, -1); HDG_Goal(false, -5);
  run(500);
  CHECK(HDG_Get().home == 1 && HDG_Get().guest == 0 && std::string(s.score) == "1    0");
  for (int i = 0; i < 12; i++) HDG_Goal(false, +1);
  run(500);
  CHECK(std::string(s.score) == "1   12");

  HDG_AdjustTime(+60, now); HDG_AdjustTime(-10, now); HDG_AdjustTime(+1, now);
  run(300);
  CHECK(timeText() == "1541" && std::string(s.time) == "1541");
  HDG_AdjustTime(-15 * 60 - 36, now);                    // auf 0:05
  run(300);
  CHECK(timeText() == " 005");

  HDG_Play(now);
  run(5200);                                             // 1. Viertel laeuft ab -> Horn 2 s, Uhr steht auf 0:00
  CHECK(HDG_Get().hornWait && HDG_Get().horn && !HDG_Get().inBreak && timeText() == " 000");
  CHECK(std::string(s.status) == "@@a" && std::string(s.time) == " 000");
  HDG_Play(now); HDG_AdjustTime(+60, now); run(100);
  CHECK(HDG_Get().hornWait && timeText() == " 000");     // Play und Korrektur wirken waehrend des Horns nicht
  run(1900);                                             // Horn aus, Viertelpause 1:00 laeuft von selbst
  CHECK(!HDG_Get().horn && std::string(s.status) == "@@A");
  CHECK(HDG_Get().inBreak && HDG_Get().running && HDG_Get().period == 1 && HDG_ShownSeconds() >= 59);
  run(60 * 1000);                                        // Pause vorbei -> Horn 1 s, 2. Viertel wartet
  CHECK(!HDG_Get().inBreak && !HDG_Get().running && HDG_Get().period == 2 && timeText() == "1500");
  CHECK(HDG_Get().horn && std::string(s.status) == "@@a");
  CHECK(std::string(s.period) == "2");
  run(1000);
  CHECK(!HDG_Get().horn && std::string(s.status) == "@@A");
  run(2000);
  CHECK(timeText() == "1500");                           // wartet auf Play

  HDG_Horn(HD_HORN_MANUAL_MS, now); run(300);            // Horn-Taste: 2 s
  CHECK(HDG_Get().horn && std::string(s.status) == "@@a");
  run(1500); CHECK(HDG_Get().horn);
  run(400);  CHECK(!HDG_Get().horn && std::string(s.status) == "@@A");

  HDG_Play(now); run(15 * 60 * 1000 + 2200);             // 2. Viertel -> Horn -> Halbzeitpause 10:00
  CHECK(HDG_Get().inBreak && HDG_Get().period == 2 && HDG_ShownSeconds() > 9 * 60 + 50);
  run(10 * 60 * 1000);
  CHECK(HDG_Get().period == 3 && !HDG_Get().running);
  HDG_Play(now); run(15 * 60 * 1000 + 2200);             // 3. Viertel -> Horn -> Viertelpause 1:00
  CHECK(HDG_Get().inBreak && HDG_ShownSeconds() <= 60);
  run(60 * 1000);
  CHECK(HDG_Get().period == 4);
  HDG_Play(now); run(15 * 60 * 1000 + 200);              // 4. Viertel -> Spielende mit Horn
  CHECK(HDG_Get().finished && !HDG_Get().running && timeText() == " 000" && std::string(s.period) == "4");
  CHECK(HDG_Get().horn && !HDG_Get().hornWait && std::string(s.status) == "@@a");
  run(2000);
  CHECK(!HDG_Get().horn && std::string(s.status) == "@@A");
  HDG_Play(now); run(1000);
  CHECK(!HDG_Get().running);                             // nach Spielende startet Play nichts

  // Neues Spiel mit anderer Spielform: 1 Abschnitt, 12 Minuten
  HdGameConfig one = { 1, 12 * 60, 0, 0 };
  CHECK(HDG_SetConfig(one));
  HDG_Reset(now); run(1500);
  CHECK(std::string(s.score) == "0    0" && std::string(s.time) == "1200" && std::string(s.period) == "1");
  HDG_Play(now); run(12 * 60 * 1000 + 200);
  CHECK(HDG_Get().finished && HDG_Get().horn);

  // Horn laeuft noch, Spielbetrieb wird beendet: die Endsequenz muss das Horn ausschalten
  HDG_Stop(now); run(1500);
  CHECK(!HDG_Get().horn && std::string(s.status) == "@@@" && !HDG_Get().active);
  HDG_Reset(now); HDG_Start(now); run(1500);
  CHECK(HDG_Get().active && std::string(s.status) == "@@A");
  HDG_Horn(HD_HORN_MANUAL_MS, now); run(300); HDG_Reset(now); run(2000);
  CHECK(!HDG_Get().horn && std::string(s.status) == "@@A");   // Neues Spiel waehrend des Horns: Horn endet normal

  // 2 x 15 mit 5 Minuten Pause
  HdGameConfig two = { 2, 15 * 60, 60, 5 * 60 };
  CHECK(HDG_SetConfig(two)); HDG_Reset(now);
  CHECK(HDG_BreakAfter(1) == 300 && HDG_BreakAfter(2) == 0);

  HdGameConfig bad = { 3, 600, 0, 0 };
  CHECK(!HDG_SetConfig(bad));

  // Ende: Anzeige geht zurueck zur Uhrzeit, danach wird nichts mehr gesendet
  CHECK(HD_GameMode(now));
  HDG_Stop(now); run(1500);
  CHECK(!HDG_Get().active && !HDG_Get().stopping && s.closed);
  CHECK(std::string(s.time) == "    " && std::string(s.score) == "      " && std::string(s.status) == "@@@");
  int before = txFrames; run(5000);
  CHECK(txFrames == before);
  CHECK(!HD_GameMode(now));
  CHECK(s.chkErrors == 0 && s.incomplete == 0);

  printf("Telegramme gesendet: %d, alle vom Dekoder angenommen: %s\n", txFrames, (s.chkErrors == 0 && s.incomplete == 0) ? "ja" : "NEIN");
  printf(fails ? "%d FEHLER\n" : "Spielbetrieb: alle Pruefungen bestanden\n", fails);
  return fails ? 1 : 0;
}
