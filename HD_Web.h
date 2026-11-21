#pragma once

#include <WebServer.h>

/*
 * Webseiten der Spielstandanzeige:
 *   /            Nachbau der Anzeige (aktualisiert sich aus den RS485-Telegrammen)
 *   /monitor     Rohdaten vom RS485-Bus mit Klartext
 *   /api/state   aktueller Zustand als JSON
 *   /api/log     mitgeschnittene Telegramme als JSON (?since=<Nummer>)
 *   /api/baud    Baudrate umstellen (?value=<Baud>)
 */
void HD_Web_Register(WebServer &srv);
