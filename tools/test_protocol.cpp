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
  const HdState &s = HD_GetState();
  printf("\nTelegramme gueltig: %u, Pruefsummenfehler: %u, unvollstaendig: %u\n", s.frames, s.chkErrors, s.incomplete);
  printf("Zustand: Zeit='%s' Tore='%s' Viertel='%s' Status='%s' Feld26='%s' closed=%d\n", s.time, s.score, s.period, s.status, s.pen, s.closed);
  return (bad || s.incomplete) ? 1 : 0;
}
