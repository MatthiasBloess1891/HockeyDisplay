#include "HD_Game.h"
#include "HD_Protocol.h"
#include <string.h>
#include <stdio.h>

#define Q_MAX    24
#define Q_FRAME  20

#define D_TIME    0x01
#define D_HOME    0x02
#define D_GUEST   0x04
#define D_PERIOD  0x08
#define D_STATUS  0x10

enum { NEXT_NONE, NEXT_BREAK, NEXT_PERIOD, NEXT_RESTART };

static HdGame   g;
static HdGameConfig nextCfg;
static uint32_t lastTickMs = 0;
static uint32_t lastTxMs = 0;
static uint32_t lastSlotMs = 0;
static uint16_t shownSec = 0xFFFF;
static uint8_t  dirty = 0;
static uint8_t  rot = 0;
static uint32_t hornEndMs = 0;
static uint8_t  hornOffRepeat = 0;
static uint32_t hornOffAtMs = 0;
static uint8_t  nextStep = NEXT_NONE;       // was nach dem Horn am Abschnittsende passiert
static uint32_t nextAtMs = 0;

static uint8_t  q[Q_MAX][Q_FRAME];
static uint8_t  qLen[Q_MAX];
static uint8_t  qHead = 0, qCount = 0;

// Lange Telegramme aus Start- und Endsequenz des Bediengeraets, Bedeutung unbekannt, unveraendert uebernommen
static const uint8_t L_SLK2[] = { 0x02, 0x00, 0x08, 0x3F, 0x3F, 0x53, 0x4C, 0x4B, 0x32, 0x03, 0x6D };
static const uint8_t L_RN[]   = { 0x02, 0x00, 0x08, 0x3F, 0x3F, 0x52, 0x4E, 0x02, 0x00, 0x03, 0x15 };
static const uint8_t L_R0A[]  = { 0x02, 0x00, 0x0E, 0x3F, 0x3F, 0x52, 0x30, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x03, 0x6E };
static const uint8_t L_R0B[]  = { 0x02, 0x00, 0x0E, 0x3F, 0x3F, 0x52, 0x30, 0x01, 0x00, 0x02, 0x01, 0x00, 0x01, 0x00, 0x00, 0x03, 0x6C };
static const uint8_t L_T0[]   = { 0x02, 0x00, 0x06, 0x3F, 0x3F, 0x54, 0x30, 0x03, 0x61 };

/***********************************************************  Texte  ***********************************************************/
uint16_t HDG_ShownSeconds() {
  int32_t ms = g.remainMs < 0 ? 0 : g.remainMs;
  uint32_t down = (uint32_t)(ms + 999) / 1000;           // aufgerundet: 15:00 beim Start, 0:00 genau am Ende
#if HD_COUNT_DOWN
  return (uint16_t)down;
#else
  if (g.inBreak) return (uint16_t)down;                  // Pausen laufen immer rueckwaerts
  return (uint16_t)(g.cfg.periodSec > down ? g.cfg.periodSec - down : 0);
#endif
}

void HDG_TimeText(char out[5]) {
  uint16_t s = HDG_ShownSeconds();
  if (s > HD_MAX_SECONDS) s = HD_MAX_SECONDS;
  snprintf(out, 5, "%2u%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

void HDG_ScoreText(char out[7]) {
  snprintf(out, 7, "%-3u%3u", (unsigned)(g.home % 1000), (unsigned)(g.guest % 1000));
}

uint16_t HDG_BreakAfter(uint8_t period) {
  if (period >= g.cfg.periods) return 0;                 // nach dem letzten Abschnitt keine Pause
  if (period * 2 == g.cfg.periods) return g.cfg.halfBreakSec;
  return g.cfg.shortBreakSec;
}

/**********************************************************  Telegramme  *******************************************************/
static void push(const uint8_t *data, size_t len) {
  if (len == 0 || len > Q_FRAME || qCount >= Q_MAX) return;
  uint8_t i = (uint8_t)((qHead + qCount) % Q_MAX);
  memcpy(q[i], data, len);
  qLen[i] = (uint8_t)len;
  qCount++;
}

static void pushShort(uint8_t field, uint8_t pos, const char *text) {
  uint8_t f[Q_FRAME];
  push(f, HD_BuildShort(field, pos, text, f, sizeof(f)));
}

static size_t buildTime(uint8_t *out, size_t max) {
  char t[5];
  HDG_TimeText(t);
  return HD_BuildShort(0x21, 0x20, t, out, max);
}

static size_t buildHome(uint8_t *out, size_t max) {
  char t[4];
  snprintf(t, sizeof(t), "%-3u", (unsigned)(g.home % 1000));
  return HD_BuildShort(0x23, 0x20, t, out, max);
}

static size_t buildGuest(uint8_t *out, size_t max) {
  char t[4];
  snprintf(t, sizeof(t), "%3u", (unsigned)(g.guest % 1000));
  return HD_BuildShort(0x23, 0x23, t, out, max);
}

static size_t buildPeriod(uint8_t *out, size_t max) {
  char t[2] = { (char)('0' + g.period), 0 };
  return HD_BuildShort(0x25, 0x20, t, out, max);
}

static size_t buildStatus(uint8_t *out, size_t max) {
  return HD_BuildShort(0x22, 0x20, g.horn ? "@@a" : "@@A", out, max);
}

static size_t buildBlock(uint8_t n, const char *text, uint8_t *out, size_t max) {
  return HD_BuildShort(0x26, (uint8_t)(0x21 + 4 * n), text, out, max);
}

/************************************************************  Spiel  **********************************************************/
static void loadPeriod(uint8_t period) {
  g.period = period;
  g.inBreak = false;
  g.finished = false;
  g.running = false;
  g.hornWait = false;
  nextStep = NEXT_NONE;
  g.remainMs = (int32_t)g.cfg.periodSec * 1000;
  dirty |= D_TIME | D_PERIOD;
}

void HDG_Horn(uint32_t durationMs, uint32_t nowMs) {
  if (!g.active || durationMs == 0) return;
  uint32_t end = nowMs + durationMs;
  if (!g.horn || (int32_t)(end - hornEndMs) > 0) hornEndMs = end;   // laufendes Horn hoechstens verlaengern
  g.horn = true;
  hornOffRepeat = 0;
  dirty |= D_STATUS;
}

static void hornOff() {
  g.horn = false;
  hornOffRepeat = 0;
}

void HDG_Init() {
  memset(&g, 0, sizeof(g));
  g.cfg.periods = 2;                                     // Voreinstellung: 2 x 15 Minuten, 5 Minuten Pause
  g.cfg.periodSec = 15 * 60;
  g.cfg.shortBreakSec = 60;
  g.cfg.halfBreakSec = 5 * 60;
  nextCfg = g.cfg;
  qHead = qCount = 0;
  dirty = 0;
  rot = 0;
  shownSec = 0xFFFF;
  hornOffRepeat = 0;
  nextStep = NEXT_NONE;
  loadPeriod(1);
  dirty = 0;
}

const HdGame &HDG_Get() { return g; }

bool HDG_SetConfig(const HdGameConfig &cfg) {
  if (cfg.periods != 1 && cfg.periods != 2 && cfg.periods != 4) return false;
  if (cfg.periodSec < 60 || cfg.periodSec > HD_MAX_SECONDS) return false;
  if (cfg.shortBreakSec > 30 * 60 || cfg.halfBreakSec > 30 * 60) return false;
  nextCfg = cfg;
  return true;
}

void HDG_Reset(uint32_t nowMs) {
  g.cfg = nextCfg;
  g.home = 0;
  g.guest = 0;
  loadPeriod(1);
  dirty |= D_HOME | D_GUEST;
  lastTickMs = nowMs;
}

void HDG_Start(uint32_t nowMs) {
  if (g.active) return;
  uint8_t f[Q_FRAME];
  qHead = qCount = 0;
  g.active = true;
  g.stopping = false;
  g.running = false;
  hornOff();
  lastTickMs = nowMs;
  // Startsequenz wie vom Bediengeraet
  pushShort(0x20, '%', "0");                             // Spielmodus EIN
  pushShort(0x22, 0x20, "@@A");                          // Status, Horn aus
  push(f, buildTime(f, sizeof(f)));
  push(f, buildPeriod(f, sizeof(f)));
  for (uint8_t i = 0; i < 4; i++) push(f, buildBlock(i, "   ", f, sizeof(f)));
  push(f, buildHome(f, sizeof(f)));
  push(L_SLK2, sizeof(L_SLK2));
  push(L_RN, sizeof(L_RN));
  push(L_R0A, sizeof(L_R0A));
  push(L_R0B, sizeof(L_R0B));
  push(f, buildGuest(f, sizeof(f)));
  dirty = 0;
  rot = 0;
  shownSec = HDG_ShownSeconds();
}

void HDG_Stop(uint32_t nowMs) {
  (void)nowMs;
  if (!g.active) return;
  uint8_t f[Q_FRAME];
  qHead = qCount = 0;
  g.active = false;
  g.stopping = true;
  g.running = false;
  g.hornWait = false;
  nextStep = NEXT_NONE;
  hornOff();                                             // die Endsequenz schaltet das Horn mit "@@@" aus
  dirty = 0;
  // Endsequenz wie vom Bediengeraet
  pushShort(0x20, '!', "0");                             // Spielmodus AUS
  push(L_T0, sizeof(L_T0));
  pushShort(0x22, 0x20, "@@@");
  pushShort(0x21, 0x20, "    ");
  pushShort(0x25, 0x20, " ");
  for (uint8_t i = 0; i < 4; i++) push(f, buildBlock(i, "   ", f, sizeof(f)));
  pushShort(0x23, 0x20, "   ");
  push(L_SLK2, sizeof(L_SLK2));
  push(L_RN, sizeof(L_RN));
  push(L_R0A, sizeof(L_R0A));
  push(L_R0B, sizeof(L_R0B));
  pushShort(0x23, 0x23, "   ");
}

void HDG_Play(uint32_t nowMs) {
  if (!g.active || g.finished || g.running || g.hornWait) return;
  g.running = true;
  lastTickMs = nowMs;
}

void HDG_Pause(uint32_t nowMs) {
  HDG_Tick(nowMs);
  g.running = false;
}

void HDG_AdjustTime(int deltaSec, uint32_t nowMs) {
  HDG_Tick(nowMs);
  if (g.hornWait) return;                                // waehrend des Horns am Abschnittsende keine Korrektur
#if !HD_COUNT_DOWN
  if (!g.inBreak) deltaSec = -deltaSec;                  // vorwaerts laufende Uhr: "+" heisst weniger Restzeit
#endif
  int32_t ms = g.remainMs + (int32_t)deltaSec * 1000;
  if (ms < 0) ms = 0;
  if (ms > (int32_t)HD_MAX_SECONDS * 1000) ms = (int32_t)HD_MAX_SECONDS * 1000;
  g.remainMs = ms;
  if (ms > 0) g.finished = false;                        // nach Spielende wieder Zeit gegeben: weiterspielen moeglich
  dirty |= D_TIME;
}

void HDG_Goal(bool home, int delta) {
  uint16_t &v = home ? g.home : g.guest;
  int n = (int)v + delta;
  if (n < 0) n = 0;
  if (n > HD_MAX_GOALS) n = HD_MAX_GOALS;
  v = (uint16_t)n;
  dirty |= home ? D_HOME : D_GUEST;
}

static void segmentEnd(uint32_t nowMs) {
  if (g.inBreak) {                                       // Pause vorbei: kurzes Horn, naechster Abschnitt wartet auf Play
    loadPeriod((uint8_t)(g.period + 1));
    HDG_Horn(HD_HORN_BREAK_MS, nowMs);
    return;
  }
  g.remainMs = 0;                                        // Spielabschnitt vorbei: Uhr bleibt auf 0:00, Horn ertoent
  g.running = false;
  HDG_Horn(HD_HORN_PERIOD_MS, nowMs);
  if (g.period >= g.cfg.periods && g.cfg.periods > 1) {  // letzter Abschnitt: Spielende
    g.finished = true;
    return;
  }
  g.hornWait = true;                                     // nach dem Horn geht es mit Pause oder naechstem Abschnitt weiter
  if (g.cfg.periods == 1) {                              // nur ein Abschnitt: danach steht die volle Zeit wieder bereit,
    nextStep = NEXT_RESTART;                             // die Tore bleiben stehen bis "Neues Spiel"
    nextAtMs = nowMs + HD_HORN_PERIOD_MS;
    return;
  }
  nextStep = HDG_BreakAfter(g.period) > 0 ? NEXT_BREAK : NEXT_PERIOD;
  nextAtMs = nowMs + HD_HORN_PERIOD_MS;
}

static void afterHorn() {
  uint8_t step = nextStep;
  nextStep = NEXT_NONE;
  g.hornWait = false;
  if (step == NEXT_BREAK) {                              // Pause startet von selbst und laeuft rueckwaerts
    g.inBreak = true;
    g.remainMs = (int32_t)HDG_BreakAfter(g.period) * 1000;
    g.running = true;
    dirty |= D_TIME;
  } else if (step == NEXT_PERIOD) {
    loadPeriod((uint8_t)(g.period + 1));
  } else if (step == NEXT_RESTART) {
    loadPeriod(1);
  }
}

void HDG_Tick(uint32_t nowMs) {
  uint32_t elapsed = nowMs - lastTickMs;
  lastTickMs = nowMs;
  if (g.active && g.running) {
    g.remainMs -= (int32_t)elapsed;
    if (g.remainMs <= 0) {
      g.remainMs = 0;
      segmentEnd(nowMs);
    }
  }
  if (g.active && nextStep != NEXT_NONE && (int32_t)(nowMs - nextAtMs) >= 0) afterHorn();
  if (g.horn && (int32_t)(nowMs - hornEndMs) >= 0) {     // Horn aus, zur Sicherheit mehrfach senden
    g.horn = false;
    dirty |= D_STATUS;
    hornOffRepeat = HD_HORN_OFF_REPEAT;
    hornOffAtMs = nowMs + 300;
  }
  if (!g.horn && hornOffRepeat > 0 && (int32_t)(nowMs - hornOffAtMs) >= 0) {
    hornOffRepeat--;
    hornOffAtMs = nowMs + 300;
    dirty |= D_STATUS;
  }
  uint16_t s = HDG_ShownSeconds();
  if (s != shownSec) {
    shownSec = s;
    dirty |= D_TIME;
  }
}

size_t HDG_NextFrame(uint32_t nowMs, uint8_t *out, size_t max) {
  if (max < Q_FRAME) return 0;
  if ((uint32_t)(nowMs - lastTxMs) < HD_TX_GAP_MS) return 0;

  if (qCount > 0) {                                      // 1. Start- oder Endsequenz
    size_t len = qLen[qHead];
    memcpy(out, q[qHead], len);
    qHead = (uint8_t)((qHead + 1) % Q_MAX);
    qCount--;
    if (qCount == 0) g.stopping = false;
    lastTxMs = nowMs;
    lastSlotMs = nowMs;
    return len;
  }
  if (!g.active) return 0;

  size_t len = 0;
  if (dirty & D_STATUS)      { dirty &= (uint8_t)~D_STATUS; len = buildStatus(out, max); }   // 2. Aenderungen sofort, Horn zuerst
  else if (dirty & D_TIME)   { dirty &= (uint8_t)~D_TIME;   len = buildTime(out, max); }
  else if (dirty & D_HOME)   { dirty &= (uint8_t)~D_HOME;   len = buildHome(out, max); }
  else if (dirty & D_GUEST)  { dirty &= (uint8_t)~D_GUEST;  len = buildGuest(out, max); }
  else if (dirty & D_PERIOD) { dirty &= (uint8_t)~D_PERIOD; len = buildPeriod(out, max); }
  else if ((uint32_t)(nowMs - lastSlotMs) >= HD_TX_SLOT_MS) {                                // 3. laufende Wiederholung
    // Reihenfolge wie im Mitschnitt: HEIM, GAST, ZEIT, HEIM/GAST im Wechsel, vier Bloecke von Feld 0x26
    uint8_t i = (uint8_t)(rot % 8);
    if (i == 0) len = buildHome(out, max);
    else if (i == 1) len = buildGuest(out, max);
    else if (i == 2) len = buildTime(out, max);
    else if (i == 3) len = (rot < 8) ? buildHome(out, max) : buildGuest(out, max);
    else len = buildBlock((uint8_t)(i - 4), "---", out, max);
    rot = (uint8_t)((rot + 1) % 16);
    lastSlotMs = nowMs;
  }
  if (len) lastTxMs = nowMs;
  return len;
}
