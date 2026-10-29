#pragma once

// ---- Settings web page ----
void webuiInit();
void webuiHandleClient();  // call every loop() - non-blocking

void requestWifiReset();  // in the .ino; reboots
void requestTouchRecalibration();
