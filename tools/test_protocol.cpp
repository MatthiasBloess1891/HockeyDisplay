// Host-Test fuer den Protokoll-Dekoder. Aufruf (im Sketch-Ordner):
//   g++ -std=c++17 -I. tools/test_protocol.cpp HD_Protocol.cpp -o /tmp/hdtest && /tmp/hdtest tools/mitschnitt.txt
// Jede Zeile der Eingabedatei ist ein Mitschnitt in Hex (ein oder mehrere Telegramme).
#include "HD_Protocol.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <map>

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "Aufruf: %s <datei>\n", argv[0]); return 2; }
  std::ifstream in(argv[1]);
  std::string line;
  uint32_t now = 1000, shown = 0;
  int bad = 0;
  std::map<std::string, int> seen;
  HD_Reset();
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    for (size_t i = 0; i + 1 < line.size(); i += 2) {
      HD_Feed((uint8_t)strtol(line.substr(i, 2).c_str(), nullptr, 16), now);
      now += 1;
    }
    now += 200;            // Pause zwischen den Zeilen
    HD_Tick(now);
    for (; shown < HD_LogHead(); shown++) {
      HdFrame f;
      if (!HD_LogGet(shown, &f)) continue;
      char hex[3 * HD_FRAME_MAX + 1] = "", desc[96];
      for (int i = 0; i < f.len; i++) sprintf(hex + strlen(hex), "%02X ", f.data[i]);
      HD_Describe(f, desc, sizeof(desc));
      if (!(f.flags & HD_F_CHK_OK)) bad++;
      if (seen[hex]++ == 0) printf("%-4s %-52s %s\n", (f.flags & HD_F_CHK_OK) ? ((f.flags & HD_F_CHK_SUBST) ? "ok*" : "ok") : "FAIL", hex, desc);
    }
  }
  // Uhr-Telegramm muss exakt dem am Bediengeraet mitgeschnittenen entsprechen (28.11.26 09:23)
  const uint8_t want[] = { 0x02, 0x3F, 0x3F, 0x52, 0x44, 0x20, 0x30, 0x32, 0x38, 0x31, 0x31, 0x32, 0x36, 0x30, 0x39, 0x32, 0x33, 0x03, 0x20 };
  uint8_t got[HD_FRAME_MAX];
  size_t n = HD_BuildClock(28, 11, 26, 9, 23, got, sizeof(got));
  bool clockOk = (n == sizeof(want)) && memcmp(got, want, n) == 0;
  printf("\nUhr-Telegramm 28.11.26 09:23: %s\n", clockOk ? "stimmt mit Mitschnitt ueberein" : "FALSCH");
  if (!clockOk) bad++;
  if (HD_BuildClock(31, 13, 26, 9, 23, got, sizeof(got)) != 0 || HD_BuildClock(1, 1, 26, 24, 0, got, sizeof(got)) != 0) { printf("Pruefung ungueltiger Werte FALSCH\n"); bad++; }

  const HdState &s = HD_GetState();
  printf("\nTelegramme gueltig: %u, Pruefsummenfehler: %u, unvollstaendig: %u\n", s.frames, s.chkErrors, s.incomplete);
  printf("Zustand: Zeit='%s' Tore='%s' Viertel='%s' Status='%s' Feld26='%s' closed=%d\n", s.time, s.score, s.period, s.status, s.pen, s.closed);
  return (bad || s.incomplete) ? 1 : 0;
}
