#pragma once

// ---------------------------------------------------------------------
// Always-on settings web page - reachable at http://<device-ip>/ or
// http://<WIFI_PORTAL_AP_NAME>.local/ once WiFi is up (separate from
// WiFiManager's setup-time captive portal).
// ---------------------------------------------------------------------
void webuiInit();
void webuiHandleClient();  // call every loop() - non-blocking

// Implemented in Volumio_Player.ino - clear persisted state and reboot,
// don't return.
void requestWifiReset();
void requestTouchRecalibration();
