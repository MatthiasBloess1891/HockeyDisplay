#pragma once

#include <WebServer.h>

/*
 * Webseiten der Spielstandanzeige:
 *   /            Nachbau der Anzeige und Bedienung des Spielbetriebs
 *   /monitor     Rohdaten vom RS485-Bus mit Klartext
 *   /uhr         Uhr der Anzeige stellen
 *   /api/state   aktueller Zustand als JSON
 *   /api/log     mitgeschnittene Telegramme als JSON (?since=<Nummer>)
 *   /api/clock   Uhr stellen (?d=TTMMJJHHMM), sendet auf dem RS485-Bus
 *   /api/game    Spielbetrieb (?cmd=start|stop|play|pause|reset|goal|time), sendet auf dem RS485-Bus
 */
void HD_Web_Register(WebServer &srv);
