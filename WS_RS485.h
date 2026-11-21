#pragma once

#include <HardwareSerial.h>       // Reference the ESP32 built-in serial port library
#include "WS_GPIO.h"
#include "HD_Protocol.h"

/*
 * RS485-Schnittstelle der Spielstandanzeige.
 *
 * Der ESP32 hoert auf dem Bus NUR mit. Es wird nichts gesendet, damit die Verbindung
 * zwischen Bediengeraet und Anzeige nicht gestoert wird.
 */

#define HD_DEFAULT_BAUD   9600    // Startwert, laesst sich auf der Monitor-Seite umstellen (wird gespeichert)

void     RS485_Init();                    // Schnittstelle oeffnen und Mithoer-Task starten
void     RS485Task(void *parameter);
uint32_t RS485_GetBaud();
bool     RS485_SetBaud(uint32_t baud);    // Baudrate umstellen und dauerhaft speichern
uint32_t RS485_RxBytes();                 // Anzahl bisher empfangener Bytes

// Zugriff auf Dekoder-Zustand und Log aus anderen Tasks nur zwischen Lock und Unlock
void     RS485_Lock();
void     RS485_Unlock();
