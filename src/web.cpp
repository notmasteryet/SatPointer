#include "web.h"

#include <ESP8266WiFi.h>

#include "calibration.h"
#include "net.h"
#include "orientation.h"
#include "rigctl.h"
#include "rotctl.h"
#include "settings.h"
#include "ui.h"

static ESP8266WebServer *srv = nullptr;

// Shown only before there is a network to reach anything else on, so it is
// deliberately the bare minimum.
static const char PORTAL_PAGE[] PROGMEM = R"HTML(<!doctype html>
<meta name=viewport content='width=device-width,initial-scale=1'><title>SatPointer</title>
<style>body{font:16px system-ui;margin:2rem auto;max-width:22rem;padding:0 1rem}
input{width:100%;padding:.6rem;margin:.3rem 0 1rem;box-sizing:border-box}
button{width:100%;padding:.7rem;font-size:1rem}</style>
<h2>SatPointer WiFi</h2><form method=post action=/save>
<label>Network</label><input name=s maxlength=32 required>
<label>Password</label><input name=p type=password maxlength=64>
<button>Save and reboot</button></form>)HTML";

// The config page. Everything fiddly lives here rather than on a 128x64 panel
// behind one knob; the device keeps the guidance screen and the target dial.
static const char CONFIG_PAGE[] PROGMEM = R"HTML(<!doctype html>
<meta name=viewport content='width=device-width,initial-scale=1'><title>SatPointer</title>
<style>
:root{color-scheme:light dark;--line:rgba(128,128,128,.35)}
body{font:15px/1.45 system-ui,sans-serif;margin:0 auto;max-width:30rem;padding:1rem 1rem 3rem}
h1{font-size:1.2rem;margin:0 0 1rem}
h2{font-size:.75rem;text-transform:uppercase;letter-spacing:.08em;opacity:.55;margin:1.6rem 0 .5rem}
.card{border:1px solid var(--line);border-radius:.6rem;padding:.9rem 1rem}
.big{font:600 1.9rem/1.15 ui-monospace,SFMono-Regular,monospace}
.sub{font:.8rem ui-monospace,monospace;opacity:.6}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:.9rem}
.row{display:flex;gap:.5rem;align-items:center;margin:.45rem 0}
.row label{flex:0 0 6.5rem;opacity:.7}
input,select,button{font:inherit;padding:.45rem .55rem;border-radius:.35rem;
 border:1px solid var(--line);background:transparent;color:inherit}
input,select{flex:1;min-width:0}
button{cursor:pointer}
button.w{width:100%;margin-top:.5rem}
.bar{height:.55rem;background:var(--line);border-radius:.3rem;overflow:hidden;flex:1}
.bar>i{display:block;height:100%;background:currentColor;width:0}
.ok{color:#18794e}.bad{color:#b3261e}.mut{opacity:.6}
small{display:block;opacity:.55;margin-top:.35rem}
</style>
<h1>SatPointer <span id=net class=sub></span></h1>

<div class=grid>
 <div class=card><div class=sub>AZIMUTH</div><div class=big id=az>--</div>
  <div class=sub>target <span id=taz>--</span></div></div>
 <div class=card><div class=sub>ELEVATION</div><div class=big id=el>--</div>
  <div class=sub>target <span id=tel>--</span></div></div>
</div>
<div class=card style=margin-top:.9rem>
 <div class=row><span class=mut>turn</span><b id=daz>--</b>
  <span class=mut style=margin-left:auto>roll</span><b id=roll>--</b></div>
 <div class=row><span class=mut>raise</span><b id=del>--</b>
  <span class=mut style=margin-left:auto>dip</span><b id=dip>--</b></div>
 <div class=row><span class=mut>field</span><b id=field>--</b>
  <span style=margin-left:auto id=calst></span></div>
</div>

<h2>Aim</h2>
<div class=card>
 <div class=row><label>Target az</label><input id=iaz type=number step=0.1 min=0 max=360></div>
 <div class=row><label>Target el</label><input id=iel type=number step=0.1 min=-90 max=90></div>
 <button class=w onclick="post('/api/target','az='+v('iaz')+'&el='+v('iel'))">Set target</button>
</div>

<h2>Calibration</h2>
<div class=card>
 <div id=calbars></div>
 <div id=calmsg class=sub></div>
 <button class=w id=calbtn onclick=cal()>Start calibration</button>
 <small>Rotate slowly through every orientation, including upside down, then
 finish. Calibrate with cables exactly as they will sit.</small>
</div>

<h2>Setup</h2>
<div class=card>
 <div class=row><label>Beam mount</label><select id=mount></select>
  <button onclick="post('/api/mount','i='+v('mount'))">Set</button></div>
 <div class=row><label>True bearing</label><input id=brg type=number step=0.1 placeholder="deg the beam points at now">
  <button onclick="post('/api/bearing','deg='+v('brg'))">Trim</button></div>
 <small>Point the beam at a known bearing, type it, and press Trim. That
 absorbs magnetic declination and mounting error together.</small>
</div>

<h2>Network</h2>
<div class=card>
 <div class=row><label>SSID</label><input id=ssid maxlength=32></div>
 <div class=row><label>Password</label><input id=pass type=password maxlength=64
  placeholder="unchanged if left blank"></div>
 <button class=w onclick="post('/api/wifi','s='+v('ssid')+'&p='+v('pass'))">Save WiFi</button>
 <button class=w id=radio onclick="post('/api/radio','on='+(S.wifi?0:1))"></button>
 <small>Turning the radio off also stops OTA and gpredict.</small>
</div>

<h2>Danger</h2>
<div class=card>
 <button class=w onclick="if(confirm('Erase calibration, trim, target and WiFi?'))post('/api/erase','')">Erase all settings</button>
</div>

<script>
var S={},touched={};
function v(id){return encodeURIComponent(document.getElementById(id).value)}
function t(id,s){document.getElementById(id).textContent=s}
function post(p,q){fetch(p+'?'+q,{method:'POST'}).then(tick)}
function cal(){post('/api/cal','do='+(S.calactive?'finish':'start'))}
['iaz','iel','ssid','pass','brg'].forEach(function(id){
  document.getElementById(id).addEventListener('input',function(){touched[id]=1})});

function tick(){fetch('/api').then(function(r){return r.json()}).then(function(d){
 S=d;
 t('net',d.state+' '+d.ip+(d.rotctl=='driving'?' · gpredict':'')+(d.freq?' · '+(d.freq/1e6).toFixed(4)+' MHz':''));
 t('az',d.fix?d.az.toFixed(1):'--'); t('el',d.fix?d.el.toFixed(1):'--');
 t('taz',d.taz.toFixed(1)); t('tel',d.tel.toFixed(1));
 t('daz',d.fix?(d.daz>0?'right ':'left ')+Math.abs(d.daz).toFixed(1):'--');
 t('del',d.fix?(d.del>0?'up ':'down ')+Math.abs(d.del).toFixed(1):'--');
 t('roll',d.fix?d.roll.toFixed(1):'--'); t('dip',d.fix?d.dip.toFixed(1):'--');
 t('field',d.fix?d.field.toFixed(1)+' uT':'--');
 var c=document.getElementById('calst');
 c.textContent=d.cal=='ok'?'cal '+d.sd.toFixed(1)+'%':(d.cal=='poor'?'BAD '+d.sd.toFixed(1)+'%':'NO CAL');
 c.className=d.cal=='ok'?'ok':'bad';
 if(!touched.iaz)document.getElementById('iaz').value=d.taz.toFixed(1);
 if(!touched.iel)document.getElementById('iel').value=d.tel.toFixed(1);
 if(!touched.ssid)document.getElementById('ssid').value=d.ssid;
 var m=document.getElementById('mount');
 if(m.options.length!=d.mounts.length){m.innerHTML='';
  d.mounts.forEach(function(n,i){var o=new Option(n,i);m.add(o)})}
 if(!touched.mount)m.value=d.mount;
 document.getElementById('radio').textContent=d.wifi?'Turn radio off':'Turn radio on';
 document.getElementById('calbtn').textContent=d.calactive?'Finish calibration':'Start calibration';
 var h='';
 if(d.calactive){d.spans.forEach(function(s,i){
   h+='<div class=row><label>'+'XYZ'[i]+' '+s+'</label><span class=bar><i style=width:'+
      Math.min(100,s/25)+'%></i></span></div>'});
  t('calmsg','samples '+d.calsamples+' — keep rotating')}
 else t('calmsg',d.calmsg);
 document.getElementById('calbars').innerHTML=h;
}).catch(function(){t('net','offline')})}
tick();setInterval(tick,700);
</script>)HTML";

static void sendJson() {
  Angles o;
  bool fix = orientationAngles(o);
  float az = fix ? trimmedAz(o) : 0;

  String j;
  j.reserve(640);
  j += "{\"state\":\""; j += netStateName();
  j += "\",\"ip\":\""; j += WiFi.localIP().toString();
  j += "\",\"ssid\":\""; j += cal.ssid;
  j += "\",\"wifi\":"; j += cal.wifiOn ? 1 : 0;
  j += ",\"rotctl\":\""; j += rotctlDriving() ? "driving" : (rotctlConnected() ? "connected" : "idle");
  j += "\",\"freq\":"; j += String(rigctlFreq(), 0);
  j += ",\"fix\":"; j += fix ? 1 : 0;
  if (fix) {
    j += ",\"az\":"; j += String(az, 2);
    j += ",\"el\":"; j += String(o.elev, 2);
    j += ",\"roll\":"; j += String(o.roll, 2);
    j += ",\"dip\":"; j += String(o.dip, 2);
    j += ",\"field\":"; j += String(o.b, 2);
    j += ",\"daz\":"; j += String(azError(az), 2);
    j += ",\"del\":"; j += String(cal.targetEl - o.elev, 2);
  }
  j += ",\"taz\":"; j += String(cal.targetAz, 2);
  j += ",\"tel\":"; j += String(cal.targetEl, 2);
  j += ",\"mount\":"; j += cal.mount;
  j += ",\"mounts\":[";
  for (uint8_t i = 0; i < mountCount(); i++) {
    if (i) j += ',';
    j += '"'; j += mountName(i); j += '"';
  }
  j += "],\"cal\":\""; j += cal.magCal ? "ok" : (cal.calSpread > 0 ? "poor" : "none");
  j += "\",\"sd\":"; j += String(cal.calSpread, 2);
  j += ",\"calactive\":"; j += calActive() ? 1 : 0;
  j += ",\"calsamples\":"; j += calSamples();
  j += ",\"spans\":["; j += calSpan(0); j += ','; j += calSpan(1); j += ','; j += calSpan(2);
  j += "],\"calmsg\":\"";
  switch (calResult()) {
    case CAL_RESULT_OK: j += "passed, sd " + String(calResultSd(), 2) + "%"; break;
    case CAL_RESULT_POOR: j += "too much spread, sd " + String(calResultSd(), 2) + "%"; break;
    case CAL_RESULT_NO_ROTATION: j += "an axis barely moved"; break;
    case CAL_RESULT_NO_SAMPLES: j += "too short to grade"; break;
    default: j += cal.magCal ? "calibrated" : "not calibrated"; break;
  }
  j += "\"}";
  srv->send(200, "application/json", j);
}

// Plain text as well as JSON: this is what a shell can read without a parser,
// and it is the only window into the device with no serial cable attached.
static void sendStatusText() {
  Angles o;
  bool fix = orientationAngles(o);
  String r;
  r.reserve(420);
  r += "state "; r += netStateName(); r += '\n';
  r += "ip "; r += WiFi.localIP().toString(); r += '\n';
  r += "ssid "; r += cal.ssid; r += '\n';
  r += "mount "; r += mountName(cal.mount); r += ' '; r += mountBeamAxis(cal.mount); r += '\n';
  r += "cal "; r += (cal.magCal ? "ok" : (cal.calSpread > 0 ? "poor" : "none"));
  r += " sd "; r += String(cal.calSpread, 2);
  r += " field "; r += String(cal.calField, 1); r += '\n';
  r += "aztrim "; r += String(cal.azTrim, 1); r += '\n';
  if (fix) {
    r += "az "; r += String(trimmedAz(o), 1); r += '\n';
    r += "el "; r += String(o.elev, 1); r += '\n';
    r += "roll "; r += String(o.roll, 1); r += '\n';
    r += "dip "; r += String(o.dip, 1); r += '\n';
    r += "field "; r += String(o.b, 1); r += '\n';
  } else {
    r += "fix none\n";
  }
  r += "target "; r += String(cal.targetAz, 1); r += ' '; r += String(cal.targetEl, 1); r += '\n';
  r += "rotctl "; r += (rotctlConnected() ? "connected" : "idle");
  r += rotctlDriving() ? " driving\n" : "\n";
  r += "rigctl "; r += (rigctlConnected() ? "connected" : "idle");
  if (rigctlFreq() > 0) { r += ' '; r += String(rigctlFreq(), 0); }
  r += '\n';
  r += "uptime "; r += millis() / 1000; r += '\n';
  r += "heap "; r += ESP.getFreeHeap(); r += '\n';
  srv->send(200, "text/plain", r);
}

static void ok() { srv->send(200, "text/plain", "ok"); }

static void handleRoot() {
  if (netState() == NET_PORTAL) srv->send_P(200, "text/html", PORTAL_PAGE);
  else srv->send_P(200, "text/html", CONFIG_PAGE);
}

static void handleSave() {
  String ssid = srv->arg("s");
  if (!ssid.length()) {
    srv->send(400, "text/plain", "network name required");
    return;
  }
  netSetCredentials(ssid.c_str(), srv->arg("p").c_str());
  srv->send(200, "text/html", "<meta charset=utf-8><h3>Saved. Rebooting.</h3>");
  delay(300);
  ESP.restart();
}

void webRegisterRoutes(ESP8266WebServer &server) {
  srv = &server;

  server.on("/", handleRoot);
  server.on("/status", sendStatusText);
  server.on("/api", sendJson);
  server.on("/save", HTTP_POST, handleSave);

  // A target typed here is a deliberate act, so unlike gpredict's stream of
  // updates it is worth persisting.
  server.on("/api/target", HTTP_POST, []() {
    if (srv->hasArg("az")) cal.targetAz = fmod(srv->arg("az").toFloat() + 360.0, 360.0);
    if (srv->hasArg("el")) cal.targetEl = constrain(srv->arg("el").toFloat(), -90.0, 90.0);
    settingsSave();
    ok();
  });

  server.on("/api/mount", HTTP_POST, []() {
    uint8_t i = srv->arg("i").toInt();
    if (i >= mountCount()) {
      srv->send(400, "text/plain", "no such mount");
      return;
    }
    uiSetMount(i);
    ok();
  });

  server.on("/api/bearing", HTTP_POST, []() {
    if (!orientationSetBearing(srv->arg("deg").toFloat())) {
      srv->send(409, "text/plain", "no valid reading yet");
      return;
    }
    ok();
  });

  server.on("/api/cal", HTTP_POST, []() {
    String what = srv->arg("do");
    if (what == "start") calStart();
    else if (what == "finish") calFinish();
    else if (what == "cancel") calCancel();
    else {
      srv->send(400, "text/plain", "start, finish or cancel");
      return;
    }
    ok();
  });

  // Blank password means "leave it alone", so the page can show the SSID
  // without ever having to round-trip the password back to the browser.
  server.on("/api/wifi", HTTP_POST, []() {
    String ssid = srv->arg("s");
    if (!ssid.length()) {
      srv->send(400, "text/plain", "network name required");
      return;
    }
    String pass = srv->arg("p");
    netSetCredentials(ssid.c_str(), pass.length() ? pass.c_str() : cal.pass);
    ok();
  });

  server.on("/api/radio", HTTP_POST, []() {
    bool on = srv->arg("on").toInt() != 0;
    ok();  // answer first: switching the radio off kills this connection
    delay(50);
    netSetEnabled(on);
  });

  server.on("/api/erase", HTTP_POST, []() {
    ok();
    delay(50);
    settingsReset();
    settingsSave();
    ESP.restart();
  });
}
