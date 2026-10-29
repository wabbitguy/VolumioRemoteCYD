#pragma once

#include <Arduino.h>
#include <FS.h>
#include "LittleFS.h"

#define VERSION "v2.1"  // shown on the web page

// ---- Language - uncomment ONE ----
#ifndef LANG_FROM_BUILD
#define LANG_EN   // English  (default)
//#define LANG_FR   // French
//#define LANG_DE   // German
//#define LANG_ES   // Spanish
//#define LANG_NL   // Dutch
//#define LANG_PT   // Portuguese
//#define LANG_TR   // Turkish
#endif

// ---- Fonts and pictures (LittleFS data folder) ----
#define AA_FONT_SMALL "NotoSansBold15"
#define AA_FONT_LARGE "NotoSansBold24"

#define buttonRewind_IMAGE  "/buttonRewind.jpg"
#define buttonPlay_IMAGE    "/buttonPlay.jpg"    // shown when NOT playing
#define buttonStop_IMAGE    "/buttonStop.jpg"    // shown when playing - see iconPathFor()
#define buttonForward_IMAGE "/buttonFastForward.jpg"  // all buttons are 34x34
#define buttonVolDown_IMAGE "/buttonVolumeDown.jpg"
#define buttonVolOff_IMAGE  "/buttonVolumeOff.jpg"
#define buttonVolUp_IMAGE   "/buttonVolumeUp.jpg"
#define buttonStationList_IMAGE "/buttonStationList.jpg"  // opens the station list

#define stationPlayIcon_IMAGE "/stationPlayIcon.jpg"  // Play icon on station-list rows

#define NO_ART_IMAGE "/noArtWork.jpg"  // shown when no cover art is found
#define webRadioPause "/webRadioPause.jpg"// used when web radio is paused/not playing

// ---- Volumio ----
#define VOLUMIO_HOST      "volumio.local"  // or Volumio's IP address
#define POLL_INTERVAL_MS  1500             // how often to poll getState
#define SCREENSAVER_POLL_INTERVAL_MS  4000  // slower polling during the screensaver
#define HTTP_TIMEOUT_MS   4000

#define ART_LOG_VERBOSE 0  // 1 = cover art debug on Serial

// ---- Display ----
#define SCREEN_W  240
#define SCREEN_H  320

// ---- Layout (Y positions) ----
#define ART_SIZE  130                       // album art render size (square, px)
#define ART_X     55                        // (SCREEN_W - ART_SIZE) / 2, centered
#define ART_Y              34  // album art top
#define Y_ARTIST          166  // top of the artist/track/album text
#define Y_PROGRESS_BAR    247  // progress/volume bar
#define Y_VOL_NUMBER      258  // Vol and Source line
#define Y_BUTTONS_BOTTOM  318  // bottom of the button row

// ---- Touch (XPT2046, own SPI bus) ----
#define TOUCH_SCLK 25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CS   33
#define TOUCH_IRQ  36

class TFT_eSPI;  // for touchCydCalibrate() below

void touchCydInit();  // call once before the others
bool touchCydGetPoint(uint16_t &sx, uint16_t &sy);  // true + screen coords if touched
bool touchCydLoadCalibration();  // true if a saved calibration was loaded
void touchCydClearCalibration();  // next boot recalibrates
void touchCydCalibrate(TFT_eSPI &tft, const char *prompt, const char *failedMsg);  // 3-crosshair calibration, saves result

// ---- Cover art lookups ----
#define ITUNES_SEARCH_HOST  "itunes.apple.com"  // no key required
#define DISCOGS_SEARCH_HOST "api.discogs.com"   // needs a personal token, settings web page

#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)
#define ITUNES_ART_SIZE STRINGIFY(ART_SIZE) "x" STRINGIFY(ART_SIZE)  // ask iTunes for ART_SIZE exactly

// ---- WiFi setup portal ----
#define WIFI_PORTAL_AP_NAME     "VolumioRemote"
#define WIFI_PORTAL_TIMEOUT_SEC 180  // reboot if setup isn't done in time

// ---- Settings (web page, saved in flash) ----
void settingsInit();  // load saved settings

String settingsGetDiscogsToken();
void   settingsSetDiscogsToken(const String &token);

bool settingsGetUseItunes();  // iTunes art lookups on/off
void settingsSetUseItunes(bool enabled);

uint32_t settingsGetScreensaverTimeoutMin();  // minutes, 0 = off
void     settingsSetScreensaverTimeoutMin(uint32_t minutes);

uint8_t settingsGetScreensaverMode();  // 0 bounce, 1 fireworks, 2 tetris, 3 starfield, 4 matrix
void    settingsSetScreensaverMode(uint8_t mode);

String settingsGetVolumioHost();  // defaults to VOLUMIO_HOST
void   settingsSetVolumioHost(const String &host);
