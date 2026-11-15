#pragma once
// The gauge's own Wi-Fi hotspot and the phone settings page.
#include <Arduino.h>

void        OilWeb_Begin();                       // load Wi-Fi name/password (creates them on first run)
void        OilWeb_Poll();                        // call every loop pass; serves requests, auto-off, deferred restarts
bool        OilWeb_On();
bool        OilWeb_Enable(bool on, const char** why);   // false + reason if it could not be switched on
void        OilWeb_FactoryResetWifi();            // Wi-Fi name/password only; nothing else is touched
const char* OilWeb_Ssid();
