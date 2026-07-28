#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------
// Runtime settings - set from the web page (webui.h/.cpp), persisted to
// flash via Preferences, namespace "settings".
// ---------------------------------------------------------------------

void settingsInit();  // load persisted values into memory - call once early in setup()

String settingsGetDiscogsToken();
void   settingsSetDiscogsToken(const String &token);

bool settingsGetUseItunes();  // iTunes Search API fallback for webradio art - see artwork.cpp
void settingsSetUseItunes(bool enabled);

// Idle timeout (minutes) before the screensaver kicks in - 0 disables it.
uint32_t settingsGetScreensaverTimeoutMin();
void     settingsSetScreensaverTimeoutMin(uint32_t minutes);

// Screensaver visual style - 0=bounce, 1=fireworks, 2=tetris, 3=starfield,
// 4=matrix. See the SS_MODE_* constants in Volumio_Player.ino.
uint8_t settingsGetScreensaverMode();
void    settingsSetScreensaverMode(uint8_t mode);

// Volumio's hostname or IP - defaults to VOLUMIO_HOST (config.h) until the
// settings page overrides it. Needed because "volumio.local" mDNS
// resolution isn't reliable on every network - see the "Volumio Host"
// section of the settings page.
String settingsGetVolumioHost();
void   settingsSetVolumioHost(const String &host);
