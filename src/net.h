// WiFi, mDNS and over-the-air updates.
//
// OTA is the point of this module as much as the network is: flashing over
// serial needs GPIO0 grounded through a reset, which on this hardware means
// physically holding a button for every single upload.
//
// The radio can be switched off from the menu, and that is not just a power
// setting. Transmit current spikes are a changing magnetic field a few
// centimetres from the magnetometer, so anyone chasing an azimuth problem
// needs to be able to take the radio out of the picture.
#pragma once

#include <Arduino.h>

enum NetState : uint8_t {
  NET_OFF,          // radio disabled in settings
  NET_CONNECTING,   // joining the stored network
  NET_ONLINE,       // joined, mDNS and OTA up
  NET_PORTAL,       // no usable credentials, running our own AP
};

void netBegin();
void netUpdate();

NetState netState();
const char *netStateName();
String netAddress();     // IP, or the AP's SSID when in portal mode
bool netOtaActive();     // true while an update is being received
uint8_t netOtaPercent();

void netSetEnabled(bool on);  // menu toggle; persists and applies immediately
void netSetCredentials(const char *ssid, const char *pass);
