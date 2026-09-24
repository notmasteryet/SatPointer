#include "net.h"

#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#include "settings.h"

static const char *HOSTNAME = "satpointer";
static const char *AP_SSID = "SatPointer-setup";
static const unsigned long CONNECT_TIMEOUT_MS = 20000;

static NetState state = NET_OFF;
static unsigned long connectStarted = 0;
static bool otaActive = false;
static uint8_t otaPercent = 0;
static ESP8266WebServer server(80);
static bool servicesUp = false;

NetState netState() { return state; }
bool netOtaActive() { return otaActive; }
uint8_t netOtaPercent() { return otaPercent; }

const char *netStateName() {
  switch (state) {
    case NET_CONNECTING: return "joining";
    case NET_ONLINE: return "online";
    case NET_PORTAL: return "setup AP";
    default: return "off";
  }
}

String netAddress() {
  if (state == NET_ONLINE) return WiFi.localIP().toString();
  if (state == NET_PORTAL) return String(AP_SSID);
  return String("-");
}

// Deliberately tiny and embedded rather than served from a filesystem: a few
// hundred bytes of HTML keeps this to one firmware image with no separate
// upload step, which matters when the whole point is to stop needing the
// serial cable.
static const char PORTAL_PAGE[] PROGMEM =
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>SatPointer</title>"
    "<style>body{font:16px system-ui;margin:2rem auto;max-width:22rem;padding:0 1rem}"
    "input{width:100%;padding:.6rem;margin:.3rem 0 1rem;box-sizing:border-box}"
    "button{width:100%;padding:.7rem;font-size:1rem}</style>"
    "<h2>SatPointer WiFi</h2><form method=post action=/save>"
    "<label>Network</label><input name=s maxlength=32 required>"
    "<label>Password</label><input name=p type=password maxlength=64>"
    "<button>Save and reboot</button></form>";

static void handleRoot() {
  server.send_P(200, "text/html", PORTAL_PAGE);
}

static void handleSave() {
  String ssid = server.arg("s");
  String pass = server.arg("p");
  if (!ssid.length()) {
    server.send(400, "text/plain", "network name required");
    return;
  }
  netSetCredentials(ssid.c_str(), pass.c_str());
  server.send(200, "text/html", "<meta charset=utf-8><h3>Saved. Rebooting.</h3>");
  delay(300);
  ESP.restart();
}

static void startPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  state = NET_PORTAL;
  Serial.printf("# wifi: no credentials, AP '%s' at %s\n", AP_SSID,
                WiFi.softAPIP().toString().c_str());
}

static void startServices() {
  if (servicesUp) return;
  servicesUp = true;

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]() {
    otaActive = true;
    otaPercent = 0;
    Serial.println(F("# ota: update starting"));
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    otaPercent = total ? done * 100 / total : 0;
  });
  ArduinoOTA.onEnd([]() {
    otaActive = false;
    Serial.println(F("# ota: done, rebooting"));
  });
  ArduinoOTA.onError([](ota_error_t e) {
    otaActive = false;
    Serial.printf("# ota: failed, error %u\n", e);
  });
  ArduinoOTA.begin();

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("arduino", "tcp", 8266);
    Serial.printf("# mdns: %s.local\n", HOSTNAME);
  }
}

static void startStation() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.begin(cal.ssid, cal.pass);
  connectStarted = millis();
  state = NET_CONNECTING;
  Serial.printf("# wifi: joining '%s'\n", cal.ssid);
}

void netBegin() {
  if (!cal.wifiOn) {
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    state = NET_OFF;
    Serial.println(F("# wifi: disabled in settings"));
    return;
  }
  WiFi.forceSleepWake();
  delay(1);
  if (cal.ssid[0]) {
    startStation();
  } else {
    startPortal();
  }
}

void netUpdate() {
  switch (state) {
    case NET_OFF:
      return;

    case NET_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        state = NET_ONLINE;
        startServices();
        Serial.printf("# wifi: online at %s\n", WiFi.localIP().toString().c_str());
      } else if (millis() - connectStarted > CONNECT_TIMEOUT_MS) {
        // Falling back to the portal rather than retrying forever: if the
        // credentials are wrong there is no way to fix them without an AP.
        Serial.println(F("# wifi: join timed out"));
        startPortal();
      }
      return;

    case NET_ONLINE:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("# wifi: link lost, rejoining"));
        startStation();
        return;
      }
      ArduinoOTA.handle();
      MDNS.update();
      return;

    case NET_PORTAL:
      server.handleClient();
      return;
  }
}

void netSetEnabled(bool on) {
  if (on == cal.wifiOn) return;
  cal.wifiOn = on;
  settingsSave();
  if (on) {
    netBegin();
  } else {
    // Radio off has to actually mean off, because the reason for switching it
    // off is magnetic interference, not power.
    if (state == NET_PORTAL) server.stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    servicesUp = false;
    state = NET_OFF;
    Serial.println(F("# wifi: off"));
  }
}

void netSetCredentials(const char *ssid, const char *pass) {
  // Zero first: strlcpy leaves the bytes past its terminator untouched, so a
  // shorter credential would otherwise keep the tail of the previous one in
  // flash.
  memset(cal.ssid, 0, sizeof(cal.ssid));
  memset(cal.pass, 0, sizeof(cal.pass));
  strlcpy(cal.ssid, ssid, sizeof(cal.ssid));
  strlcpy(cal.pass, pass, sizeof(cal.pass));
  settingsSave();
  Serial.printf("# wifi: credentials for '%s' saved\n", cal.ssid);
}
