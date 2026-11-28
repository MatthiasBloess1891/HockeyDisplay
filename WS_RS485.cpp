#include "WS_RS485.h"

static HardwareSerial hdSerial(1);          // UART1 am RS485-Baustein
static SemaphoreHandle_t hdMutex = NULL;
static volatile uint32_t rxBytes = 0;
static uint32_t lastRxMs = 0;               // Zeitpunkt des letzten empfangenen Bytes

void RS485_Lock()   { if (hdMutex) xSemaphoreTake(hdMutex, portMAX_DELAY); }
void RS485_Unlock() { if (hdMutex) xSemaphoreGive(hdMutex); }

uint32_t RS485_GetBaud() { return HD_BAUD; }
uint32_t RS485_RxBytes() { return rxBytes; }

void RS485_Init()                                             // Initializing serial port
{
  hdMutex = xSemaphoreCreateMutex();
  HD_Reset();

  hdSerial.begin(HD_BAUD, SERIAL_8N1, RXD1, TXD1);           // gesendet wird nur in RS485_SendClock
  printf("RS485: Mithoeren mit %lu Baud 8N1\r\n", (unsigned long)HD_BAUD);

  xTaskCreatePinnedToCore(
    RS485Task,
    "RS485Task",
    4096,
    NULL,
    3,
    NULL,
    0
  );
}

int RS485_SendClock(int day, int month, int year2, int hour, int minute, uint8_t *frame, size_t *frameLen) {
  uint8_t buf[HD_FRAME_MAX];
  size_t len = HD_BuildClock(day, month, year2, hour, minute, buf, sizeof(buf));
  if (len == 0) return HD_SEND_INVALID;
  if (frame && frameLen) {
    memcpy(frame, buf, len);
    *frameLen = len;
  }

  for (int tries = 0; tries < 50; tries++) {                  // bis zu 0,5 s auf einen ruhigen Bus warten
    RS485_Lock();
    uint32_t now = millis();
    if (HD_Online(now)) {                                     // Spielbetrieb: nicht dazwischenfunken
      RS485_Unlock();
      return HD_SEND_GAME;
    }
    if (hdSerial.available() == 0 && (uint32_t)(now - lastRxMs) >= HD_BUS_IDLE_MS) {
      hdSerial.write(buf, len);
      hdSerial.flush();                                       // warten, bis alles gesendet ist
      HD_LogSent(buf, len, millis());
      RS485_Unlock();
      printf("RS485: Uhr gestellt auf %02d.%02d.%02d %02d:%02d\r\n", day, month, year2, hour, minute);
      return HD_SEND_OK;
    }
    RS485_Unlock();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return HD_SEND_BUSY;
}

void RS485Task(void *parameter) {
  while (1) {
    RS485_Lock();
    uint32_t now = millis();
    while (hdSerial.available() > 0) {
      HD_Feed((uint8_t)hdSerial.read(), now);
      rxBytes++;
      lastRxMs = now;
    }
    HD_Tick(now);
    RS485_Unlock();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  vTaskDelete(NULL);
}
