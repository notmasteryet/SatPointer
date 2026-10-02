// The TCP plumbing the hamlib emulators share: one client at a time, input
// split into lines, and every reply sent as a single write.
//
// That last part is the reason this exists. gpredict reads each reply with a
// single recv(), and with Nagle off every print is its own packet - a reply
// built from several prints arrives in pieces and gpredict shows ERROR.
#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>

class LineServer {
 public:
  typedef void (*Handler)(String &cmd);
  typedef void (*Hook)();

  // onConnect may be null; it runs before the new client's first line.
  LineServer(uint16_t port, const char *name, Handler handler, Hook onConnect = nullptr);

  void update();  // every loop; follows the network up and down on its own
  bool connected();
  void reply(const char *s);
  void close();

 private:
  WiFiServer server;
  WiFiClient client;
  const char *name;
  uint16_t port;
  Handler handler;
  Hook onConnect;
  bool started = false;
  String line;
};
