#include "WS_RS485.h"
#include <Preferences.h>

static HardwareSerial hdSerial(1);          // UART1 am RS485-Baustein
static SemaphoreHandle_t hdMutex = NULL;
static uint32_t Baudrate = HD_DEFAULT_BAUD;
static volatile uint32_t rxBytes = 0;

static const uint32_t allowedBaud[] = { 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };

static bool baudAllowed(uint32_t baud) {
  for (size_t i = 0; i < sizeof(allowedBaud) / sizeof(allowedBaud[0]); i++) {
    if (allowedBaud[i] == baud) return true;
  }
  return false;
}

void RS485_Lock()   { if (hdMutex) xSemaphoreTake(hdMutex, portMAX_DELAY); }
void RS485_Unlock() { if (hdMutex) xSemaphoreGive(hdMutex); }

uint32_t RS485_GetBaud() { return Baudrate; }
uint32_t RS485_RxBytes() { return rxBytes; }

bool RS485_SetBaud(uint32_t baud) {
  if (!baudAllowed(baud)) return false;
  RS485_Lock();
  Baudrate = baud;
  hdSerial.updateBaudRate(baud);
  RS485_Unlock();
  Preferences prefs;
  prefs.begin("hockey", false);
  prefs.putUInt("baud", baud);
  prefs.end();
  printf("RS485: Baudrate %lu\r\n", (unsigned long)baud);
  return true;
}

void RS485_Init()                                             // Initializing serial port
{
  hdMutex = xSemaphoreCreateMutex();
  HD_Reset();

  Preferences prefs;
  prefs.begin("hockey", true);
  uint32_t saved = prefs.getUInt("baud", HD_DEFAULT_BAUD);
  prefs.end();
  Baudrate = baudAllowed(saved) ? saved : HD_DEFAULT_BAUD;

  hdSerial.begin(Baudrate, SERIAL_8N1, RXD1, TXD1);           // TXD bleibt in Ruhe (High), es wird nie gesendet
  printf("RS485: Mithoeren mit %lu Baud 8N1\r\n", (unsigned long)Baudrate);

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

void RS485Task(void *parameter) {
  while (1) {
    RS485_Lock();
    uint32_t now = millis();
    while (hdSerial.available() > 0) {
      HD_Feed((uint8_t)hdSerial.read(), now);
      rxBytes++;
    }
    HD_Tick(now);
    RS485_Unlock();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  vTaskDelete(NULL);
}
