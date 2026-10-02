#include "lineserver.h"

#include "net.h"

LineServer::LineServer(uint16_t port, const char *name, Handler handler, Hook onConnect)
    : server(port), name(name), port(port), handler(handler), onConnect(onConnect) {}

bool LineServer::connected() { return client && client.connected(); }

void LineServer::reply(const char *s) { client.write(s, strlen(s)); }

void LineServer::close() { client.stop(); }

void LineServer::update() {
  if (netState() != NET_ONLINE) {
    if (started) {
      client.stop();
      server.stop();
      started = false;
    }
    return;
  }

  if (!started) {
    server.begin();
    server.setNoDelay(true);
    started = true;
    Serial.printf("# %s: listening on %u\n", name, port);
  }

  if (!connected()) {
    WiFiClient fresh = server.available();
    if (fresh) {
      if (client) client.stop();
      client = fresh;
      client.setNoDelay(true);
      line = "";
      if (onConnect) onConnect();
      Serial.printf("# %s: client connected\n", name);
    }
  }

  while (connected() && client.available()) {
    char ch = client.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      String done = line;
      line = "";
      done.trim();
      if (done.length()) handler(done);
    } else if (line.length() < 80) {
      line += ch;
    }
  }
}
