#pragma once

#include <HardwareSerial.h>       // Reference the ESP32 built-in serial port library
#include "WS_GPIO.h"
#include "HD_Protocol.h"

/*
 * RS485-Schnittstelle der Spielstandanzeige.
 *
 * Der ESP32 hoert auf dem Bus mit. Gesendet wird ausschliesslich das Telegramm zum Stellen
 * der Uhr, und nur wenn das Bediengeraet nicht im Spielmodus sendet und der Bus ruhig ist.
 */

#define HD_DEFAULT_BAUD   9600    // Startwert, laesst sich auf der Monitor-Seite umstellen (wird gespeichert)
#define HD_BUS_IDLE_MS    100     // so lange muss der Bus vor dem Senden ruhig sein

// Ergebnis von RS485_SendClock
#define HD_SEND_OK        0
#define HD_SEND_INVALID   1       // Datum oder Uhrzeit ungueltig
#define HD_SEND_GAME      2       // Bediengeraet sendet Spieltelegramme, es wird nicht gesendet
#define HD_SEND_BUSY      3       // Bus wurde nicht ruhig

void     RS485_Init();                    // Schnittstelle oeffnen und Mithoer-Task starten
void     RS485Task(void *parameter);
uint32_t RS485_GetBaud();
bool     RS485_SetBaud(uint32_t baud);    // Baudrate umstellen und dauerhaft speichern
uint32_t RS485_RxBytes();                 // Anzahl bisher empfangener Bytes

// Uhr der Anzeige stellen. frame/frameLen liefern das gesendete Telegramm zurueck (duerfen NULL sein)
int      RS485_SendClock(int day, int month, int year2, int hour, int minute, uint8_t *frame, size_t *frameLen);

// Zugriff auf Dekoder-Zustand und Log aus anderen Tasks nur zwischen Lock und Unlock
void     RS485_Lock();
void     RS485_Unlock();
