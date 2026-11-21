#pragma once

#define Extension_Enable      1                   // Whether to extend the connection to external devices     1:Expansion device Modbus RTU Relay     0:No extend
#define RS485_CAN_Enable      1                   // This item is configured according to product selection   1:Select RS485                          0:Select CAN
#define RTC_Event_Enable      1                   // Whether to enable RTC events  (Bluetooth)                1:Enable                                0:Disable



// WLAN-Zugangsdaten und OTA-Passwort stehen in secrets.h. Diese Datei wird NICHT zu GitHub
// hochgeladen (.gitignore). Vorlage: secrets.example.h kopieren, in secrets.h umbenennen, ausfuellen.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "secrets.h fehlt: secrets.example.h kopieren, in secrets.h umbenennen und Zugangsdaten eintragen"
#endif

// Netzwerk
#define HD_HOSTNAME       "hockeydisplay"         // Name im Netzwerk und fuer das OTA-Update
#define HD_USE_STATIC_IP  1                       // 1: feste IP-Adresse im WLAN    0: Adresse per DHCP
#define HD_IP             192,168,1,16
#define HD_GATEWAY        192,168,1,1
#define HD_SUBNET         255,255,255,0
#define HD_DNS            192,168,1,1

// Details about devices on the Waveshare cloud
#define MQTT_Server   "mqtt.waveshare.cloud"
#define MQTT_Port     1883
#define MQTT_ID       "fc2d8db5"
#define MQTT_Pub      "Pub/59/54/fc2d8db5"
#define MQTT_Sub      "Sub/59/54/fc2d8db5"
