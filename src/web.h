// Everything served over HTTP: the setup form, the config page and its API.
//
// Kept apart from net.cpp, which owns getting and staying on the network. The
// server object still lives there because the portal needs it before any of
// this is reachable.
#pragma once

#include <ESP8266WebServer.h>

void webRegisterRoutes(ESP8266WebServer &server);
