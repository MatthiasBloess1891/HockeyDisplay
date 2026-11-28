#include "HD_Protocol.h"
#include <string.h>
#include <stdio.h>

#define STX 0x02
#define ETX 0x03

enum HdRxState { S_IDLE, S_HEAD, S_SHORT, S_SHORT_BCC, S_LONG_LEN, S_LONG };

static HdRxState rxState = S_IDLE;
static uint8_t   rxBuf[HD_FRAME_MAX];
static uint8_t   rxLen = 0;
static uint16_t  rxNeed = 0;
static uint32_t  rxLastMs = 0;

static HdState   state;
static HdFrame   logBuf[HD_LOG_SIZE];
static uint32_t  logHead = 0;                 // Nummer des naechsten Telegramms

/******************************************************  Hilfsfunktionen  ******************************************************/
uint8_t HD_Bcc(const uint8_t *p, size_t n) {
  uint8_t x = 0;
  for (size_t i = 0; i < n; i++) x ^= p[i];
  return x;
}

static void fill(char *dst, size_t n) {
  memset(dst, ' ', n);
  dst[n] = 0;
}

// Text an Position pos in ein Textfeld der Laenge size schreiben
static void writeField(char *dst, size_t size, uint8_t pos, const uint8_t *text, size_t len) {
  for (size_t i = 0; i < len && pos + i < size; i++) {
    uint8_t c = text[i];
    dst[pos + i] = (c >= 0x20 && c <= 0x7E) ? (char)c : '?';
  }
}

void HD_FormatTime(const char *raw4, char *out, size_t n) {
  // 4 Zeichen: 2 Stellen Minuten, 2 Stellen Sekunden (" 020" = 0:20)
  if (raw4[0] == ' ' && raw4[1] == ' ' && raw4[2] == ' ' && raw4[3] == ' ') {
    snprintf(out, n, "(leer)");
    return;
  }
  char m10 = raw4[0], m1 = raw4[1] == ' ' ? '0' : raw4[1], s10 = raw4[2] == ' ' ? '0' : raw4[2];
  if (m10 == ' ') snprintf(out, n, "%c:%c%c", m1, s10, raw4[3]);
  else snprintf(out, n, "%c%c:%c%c", m10, m1, s10, raw4[3]);
}

static void trimCopy(const char *src, size_t len, char *out, size_t n) {
  size_t a = 0, b = len;
  while (a < b && src[a] == ' ') a++;
  while (b > a && src[b - 1] == ' ') b--;
  size_t k = 0;
  for (size_t i = a; i < b && k + 1 < n; i++) out[k++] = src[i];
  out[k] = 0;
}

/**********************************************************  Zustand  **********************************************************/
static void resetState() {
  memset(&state, 0, sizeof(state));
  fill(state.time, 4);
  fill(state.status, 3);
  fill(state.score, 6);
  fill(state.period, 1);
  fill(state.pen, 16);
  state.mode = ' ';
}

void HD_Reset() {
  rxState = S_IDLE;
  rxLen = 0;
  resetState();
  logHead = 0;
}

const HdState &HD_GetState() { return state; }

bool HD_Online(uint32_t nowMs) {
  return state.gameSeen && (uint32_t)(nowMs - state.lastGameMs) < HD_TIMEOUT_MS;
}

bool HD_GameMode(uint32_t nowMs) {
  return HD_Online(nowMs) && !state.closed;
}

static bool isClockFrame(const uint8_t *d, uint8_t len) {
  if (len != 19 || d[5] != 0x20 || d[6] != '0') return false;
  for (int i = 7; i < 17; i++) if (d[i] < '0' || d[i] > '9') return false;
  return true;
}

// Gueltiges Kurztelegramm "??RD" auf den Zustand anwenden. Rueckgabe: Bedeutung bekannt
static bool applyShort(const uint8_t *d, uint8_t len, uint32_t nowMs) {
  if (len < 9 || d[3] != 'R' || d[4] != 'D') return false;
  uint8_t field = d[5];
  uint8_t pos = (uint8_t)(d[6] - 0x20);
  const uint8_t *text = d + 7;
  size_t tlen = (size_t)len - 9;                // ohne Kopf (7), ETX und BCC
  if (field >= 0x21 && field <= 0x26) {         // Spieltelegramm: Bediengeraet ist im Spielmodus
    state.gameSeen = true;
    state.lastGameMs = nowMs;
  }
  switch (field) {
    case 0x20:                                  // Steuerung: '%' Spielmodus ein, '!' aus, '0' Uhr stellen
      if (d[6] == '%') { state.mode = '%'; state.closed = false; state.gameSeen = true; state.lastGameMs = nowMs; }
      if (d[6] == '!') { state.mode = '!'; state.closed = true; }
      return d[6] == '%' || d[6] == '!' || isClockFrame(d, len);
    case 0x21: writeField(state.time, 4, pos, text, tlen);   return true;
    case 0x22: writeField(state.status, 3, pos, text, tlen); return true;
    case 0x23: writeField(state.score, 6, pos, text, tlen);  return true;
    case 0x25: writeField(state.period, 1, pos, text, tlen); return true;
    case 0x26: writeField(state.pen, 16, pos, text, tlen);   return true;
  }
  return false;
}

/************************************************************  Log  ************************************************************/
uint32_t HD_LogHead() { return logHead; }

void HD_LogClear() {
  for (int i = 0; i < HD_LOG_SIZE; i++) logBuf[i].len = 0;
}

bool HD_LogGet(uint32_t seq, HdFrame *out) {
  if (seq >= logHead || logHead - seq > HD_LOG_SIZE) return false;
  const HdFrame &f = logBuf[seq % HD_LOG_SIZE];
  if (f.seq != seq || f.len == 0) return false;
  *out = f;
  return true;
}

static void emit(uint8_t flags, uint32_t nowMs) {
  if (rxLen == 0) return;
  bool isLong = (rxState == S_LONG_LEN || rxState == S_LONG);
  if (isLong) flags |= HD_F_LONG;

  if (!(flags & (HD_F_INCOMPLETE | HD_F_NOISE)) && rxLen >= 4) {
    uint8_t calc = HD_Bcc(rxBuf + 1, rxLen - 2);          // alles nach STX bis einschliesslich ETX
    uint8_t got = rxBuf[rxLen - 1];
    bool etxOk = (rxBuf[rxLen - 2] == ETX);
    if (etxOk && calc == got) {
      flags |= HD_F_CHK_OK;
    } else if (etxOk && calc <= 0x04 && got == 0x20) {
      flags |= HD_F_CHK_OK | HD_F_CHK_SUBST;
    }
    if (flags & HD_F_CHK_OK) {
      state.frames++;
      state.seen = true;
      state.lastValidMs = nowMs;
      if (!isLong && applyShort(rxBuf, rxLen, nowMs)) flags |= HD_F_KNOWN;
    } else {
      state.chkErrors++;
    }
  } else {
    state.incomplete++;
  }

  HdFrame &f = logBuf[logHead % HD_LOG_SIZE];
  f.seq = logHead;
  f.ms = nowMs;
  f.len = rxLen;
  f.flags = flags;
  memcpy(f.data, rxBuf, rxLen);
  logHead++;

  rxLen = 0;
  rxState = S_IDLE;
}

/**********************************************************  Senden  ***********************************************************/
size_t HD_BuildShort(uint8_t field, uint8_t pos, const char *text, uint8_t *out, size_t max) {
  size_t tlen = strlen(text);
  if (tlen + 9 > max) return 0;
  size_t n = 0;
  out[n++] = STX;
  out[n++] = '?';
  out[n++] = '?';
  out[n++] = 'R';
  out[n++] = 'D';
  out[n++] = field;
  out[n++] = pos;
  for (size_t i = 0; i < tlen; i++) out[n++] = (uint8_t)text[i];
  out[n++] = ETX;
  uint8_t bcc = HD_Bcc(out + 1, n - 1);
  out[n++] = (bcc <= 0x04) ? 0x20 : bcc;        // 0x00..0x04 wird als 0x20 gesendet
  return n;
}

// Uhr stellen: Feld 0x20, Kennung '0', Text TTMMJJHHMM (ohne Sekunden)
size_t HD_BuildClock(int day, int month, int year2, int hour, int minute, uint8_t *out, size_t max) {
  if (day < 1 || day > 31 || month < 1 || month > 12 || year2 < 0 || year2 > 99) return 0;
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return 0;
  char text[11];
  snprintf(text, sizeof(text), "%02d%02d%02d%02d%02d", day, month, year2, hour, minute);
  return HD_BuildShort(0x20, '0', text, out, max);
}

void HD_LogSent(const uint8_t *data, size_t len, uint32_t nowMs) {
  if (len == 0 || len > HD_FRAME_MAX) return;
  HdFrame &f = logBuf[logHead % HD_LOG_SIZE];
  f.seq = logHead;
  f.ms = nowMs;
  f.len = (uint8_t)len;
  f.flags = HD_F_TX | HD_F_CHK_OK;
  if (len > 2 && data[1] == 0x00) f.flags |= HD_F_LONG;   // langes Telegramm
  else f.flags |= HD_F_KNOWN;
  memcpy(f.data, data, len);
  logHead++;
}

/**********************************************************  Empfang  **********************************************************/
void HD_Tick(uint32_t nowMs) {
  if (rxLen > 0 && (uint32_t)(nowMs - rxLastMs) > HD_GAP_MS) {
    emit(rxState == S_IDLE ? HD_F_NOISE : HD_F_INCOMPLETE, nowMs);
  }
}

void HD_Feed(uint8_t b, uint32_t nowMs) {
  HD_Tick(nowMs);
  rxLastMs = nowMs;

  switch (rxState) {
    case S_IDLE:
      if (b == STX) {
        emit(HD_F_NOISE, nowMs);                // vorher aufgelaufene Stoerbytes abschliessen
        rxBuf[0] = b;
        rxLen = 1;
        rxState = S_HEAD;
      } else {
        rxBuf[rxLen++] = b;
        if (rxLen >= HD_FRAME_MAX) emit(HD_F_NOISE, nowMs);
      }
      break;

    case S_HEAD:
      rxBuf[rxLen++] = b;
      if (b == 0x00) rxState = S_LONG_LEN;      // langes Telegramm: 2 Byte Laenge, High-Byte 0
      else if (b == ETX) rxState = S_SHORT_BCC;
      else rxState = S_SHORT;
      break;

    case S_SHORT:
      if (b == STX) {                           // neuer Telegrammanfang mitten im Telegramm: neu aufsetzen
        emit(HD_F_INCOMPLETE, nowMs);
        rxBuf[0] = b;
        rxLen = 1;
        rxState = S_HEAD;
        break;
      }
      rxBuf[rxLen++] = b;
      if (b == ETX) rxState = S_SHORT_BCC;
      else if (rxLen >= HD_FRAME_MAX - 1) emit(HD_F_INCOMPLETE, nowMs);
      break;

    case S_SHORT_BCC:
      rxBuf[rxLen++] = b;
      emit(0, nowMs);
      break;

    case S_LONG_LEN:
      rxBuf[rxLen++] = b;
      rxNeed = b;
      if (rxNeed < 2 || rxNeed > HD_FRAME_MAX - 3) emit(HD_F_INCOMPLETE, nowMs);
      else rxState = S_LONG;
      break;

    case S_LONG:
      rxBuf[rxLen++] = b;
      if (--rxNeed == 0) emit(0, nowMs);
      break;
  }
}

/**********************************************************  Klartext  *********************************************************/
static void printable(const uint8_t *p, size_t len, char *out, size_t n) {
  size_t k = 0;
  for (size_t i = 0; i < len && k + 1 < n; i++) out[k++] = (p[i] >= 0x20 && p[i] <= 0x7E) ? (char)p[i] : '.';
  out[k] = 0;
}

static size_t describeRx(const HdFrame &f, char *out, size_t n);

size_t HD_Describe(const HdFrame &f, char *out, size_t n) {
  if (!(f.flags & HD_F_TX)) return describeRx(f, out, n);
  size_t k = (size_t)snprintf(out, n, "GESENDET: ");
  if (k >= n) return k;
  return k + describeRx(f, out + k, n - k);
}

static size_t describeRx(const HdFrame &f, char *out, size_t n) {
  const uint8_t *d = f.data;
  char txt[HD_FRAME_MAX + 1];
  char tmp[16];

  if (f.flags & HD_F_NOISE)      return (size_t)snprintf(out, n, "Bytes ausserhalb eines Telegramms");
  if (f.flags & HD_F_INCOMPLETE) return (size_t)snprintf(out, n, "Telegramm unvollstaendig");
  if (!(f.flags & HD_F_CHK_OK))  return (size_t)snprintf(out, n, "Pruefsumme falsch (erwartet %02X)", HD_Bcc(d + 1, f.len - 2));

  if (f.flags & HD_F_LONG) {
    printable(d + 3, (size_t)f.len - 5, txt, sizeof(txt));
    return (size_t)snprintf(out, n, "Lang, unbekannt: \"%s\"", txt);
  }

  if (f.len < 9 || d[3] != 'R' || d[4] != 'D') {
    printable(d + 1, (size_t)f.len - 3, txt, sizeof(txt));
    return (size_t)snprintf(out, n, "Unbekannt: \"%s\"", txt);
  }

  uint8_t field = d[5], pos = d[6];
  size_t tlen = (size_t)f.len - 9;
  printable(d + 7, tlen, txt, sizeof(txt));

  switch (field) {
    case 0x20:
      if (pos == '%') return (size_t)snprintf(out, n, "Spielmodus EIN (Start)");
      if (pos == '!') return (size_t)snprintf(out, n, "Spielmodus AUS (zurueck zur Uhrzeit)");
      if (isClockFrame(d, f.len)) {
        return (size_t)snprintf(out, n, "Uhr stellen %c%c.%c%c.%c%c %c%c:%c%c", txt[0], txt[1], txt[2], txt[3], txt[4], txt[5], txt[6], txt[7], txt[8], txt[9]);
      }
      return (size_t)snprintf(out, n, "Betriebsart 0x%02X \"%s\"", pos, txt);
    case 0x21:
      if (pos == 0x20 && tlen == 4) {
        HD_FormatTime(txt, tmp, sizeof(tmp));
        return (size_t)snprintf(out, n, "Spielzeit %s", tmp);
      }
      break;
    case 0x22:
      if (pos == 0x20 && tlen == 3) {
        if (d[9] == 0x61) return (size_t)snprintf(out, n, "Horn EIN");
        if (d[9] == 0x41) return (size_t)snprintf(out, n, "Horn AUS");
        if (d[9] == 0x40) return (size_t)snprintf(out, n, "Horn AUS, Status 0x40 (Spielende)");
        return (size_t)snprintf(out, n, "Status \"%s\"", txt);
      }
      break;
    case 0x23:
      if (tlen == 3 && (pos == 0x20 || pos == 0x23)) {
        trimCopy(txt, 3, tmp, sizeof(tmp));
        return (size_t)snprintf(out, n, "Tore %s %s", pos == 0x20 ? "HEIM" : "GAST", tmp[0] ? tmp : "(leer)");
      }
      break;
    case 0x25:
      if (pos == 0x20 && tlen == 1) return (size_t)snprintf(out, n, "Viertel %s", txt[0] == ' ' ? "(leer)" : txt);
      break;
    case 0x26:
      return (size_t)snprintf(out, n, "Feld 0x26 Block %d \"%s\" (Strafzeit?)", (pos - 0x21) / 4 + 1, txt);
  }
  return (size_t)snprintf(out, n, "Feld 0x%02X Pos %d \"%s\"", field, pos - 0x20, txt);
}
