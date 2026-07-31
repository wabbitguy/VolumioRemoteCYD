#pragma once

#include <FS.h>
#include "LittleFS.h"

#define VERSION "v1.8"  // shown on the settings web page footer - bump by hand each release

// ---------------------------------------------------------------------
// Fonts (LittleFS /data folder)
// ---------------------------------------------------------------------
#define AA_FONT_SMALL "NotoSansBold15"
#define AA_FONT_LARGE "NotoSansBold24"

#define buttonRewind_IMAGE  "/buttonRewind.jpg"
#define buttonPlay_IMAGE    "/buttonPlay.jpg"    // shown when NOT playing
#define buttonStop_IMAGE    "/buttonStop.jpg"    // shown when playing - see iconPathFor()
#define buttonForward_IMAGE "/buttonFastForward.jpg"  // all 34x34 icons - see BTN_ICON_SRC in the .ino
#define buttonVolDown_IMAGE "/buttonVolumeDown.jpg"
#define buttonVolOff_IMAGE  "/buttonVolumeOff.jpg"
#define buttonVolUp_IMAGE   "/buttonVolumeUp.jpg"
#define buttonStationList_IMAGE "/buttonStationList.jpg"  // opens the favorite-webradio station switcher

// Last-resort cover art fallback - must be JPEG. Optional: box is left
// black if missing from LittleFS /data.
#define NO_ART_IMAGE "/noArtWork.jpg"
#define webRadioPause "/webRadioPause.jpg"// used when web radio is paused/not playing

// ---------------------------------------------------------------------
// Volumio
// ---------------------------------------------------------------------
#define VOLUMIO_HOST      "volumio.local"  // swap for Volumio's IP if mDNS resolution is flaky
#define POLL_INTERVAL_MS  1500             // how often to poll getState
#define SCREENSAVER_POLL_INTERVAL_MS  4000 // slower cadence while the screensaver is up - see loop()
#define HTTP_TIMEOUT_MS   4000

// 1 = verbose "Art: ..." Serial output for cover-art lookups/fetches (handy
// when chasing a bad match or a decode failure), 0 = quiet during normal use.
#define ART_LOG_VERBOSE 0

// ---------------------------------------------------------------------
// Display (CYD 2.8" - ESP32-2432S028R, 240x320 portrait, ST7789 +
// resistive touch). 
// ---------------------------------------------------------------------
#define SCREEN_W  240
#define SCREEN_H  320

// ---------------------------------------------------------------------
// Layout - vertical (Y) anchors, one independently-tunable value per
// screen element. Each is a flat number, not derived from the others -
// edit any one without needing to re-derive the rest. (X positions are
// unchanged from before this: TEXT_X above, BTN_X0-5/VOL_NUMBER_X in the
// .ino.) Top to bottom on screen:
//   ART_Y (above)     - album art top edge
//   Y_ARTIST          - top of the text block (artist/track/album lines).
//                        Everything from here down to Y_PROGRESS_BAR is
//                        laid out by drawTextBlock() in the .ino - real
//                        per-line font heights measured, then whatever
//                        space is left is split evenly between whichever
//                        lines are actually active.
//   Y_PROGRESS_BAR    - progress/volume-slider bar. Doubles as the hard
//                        floor the text block above lays out against.
//   Y_VOL_NUMBER      - "Vol NN" text, and now "Source: ..." too (see
//                        SOURCE_MAXW in the .ino) - moved here off the text
//                        block above to free up a whole line's worth of
//                        room in that tight space.
//   Y_BUTTONS_BOTTOM  - bottom edge of the button icon row
// ---------------------------------------------------------------------
#define ART_SIZE  130                       // album art render size (square, px)
#define ART_X     55                        // (SCREEN_W - ART_SIZE) / 2, centered
#define ART_Y              34                        // leaves room above for the header line - also "Artwork TOP"
#define Y_ARTIST          166  // 2px below the album art's bottom edge (ART_Y + ART_SIZE = 164)
#define Y_PROGRESS_BAR    247  // bar is 8px tall now (PROGRESS_H, .ino) - bottom edge still lands at 255, same spot as before
#define Y_VOL_NUMBER      258
#define Y_BUTTONS_BOTTOM  318

// ---------------------------------------------------------------------
// Touch (XPT2046, resistive) - wired to a separate SPI bus from the
// display on this board, so it can't use TFT_eSPI's built-in touch
// ---------------------------------------------------------------------
#define TOUCH_SCLK 25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CS   33
#define TOUCH_IRQ  36

class TFT_eSPI;  // forward decl - only needed for touchCydCalibrate()'s signature below

void touchCydInit();                                // call once from displayInit(), before any of the below
bool touchCydGetPoint(uint16_t &sx, uint16_t &sy);   // true + calibrated/clamped screen coords if touched, false otherwise
bool touchCydLoadCalibration();                      // true if a saved calibration was found and applied
void touchCydClearCalibration();                     // delete saved calibration - next boot re-runs it
void touchCydCalibrate(TFT_eSPI &tft);               // interactive 3-crosshair routine, draws on tft, saves result - blocks until done

// ---------------------------------------------------------------------
// Cover art fallbacks (webradio/library art) - see artwork.cpp
// ---------------------------------------------------------------------
#define ITUNES_SEARCH_HOST  "itunes.apple.com"  // no key required
#define DISCOGS_SEARCH_HOST "api.discogs.com"   // needs a personal token, settings web page

// Ask iTunes for exactly ART_SIZE so fetchAndDraw's fitScale() picks scale 1
// (no downscale) - anything bigger only lands on power-of-2 steps, well
// short of the box.
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)
#define ITUNES_ART_SIZE STRINGIFY(ART_SIZE) "x" STRINGIFY(ART_SIZE)

// ---------------------------------------------------------------------
// WiFi (captive portal via WiFiManager) - no hardcoded credentials
// ---------------------------------------------------------------------
#define WIFI_PORTAL_AP_NAME     "VolumioRemote"
#define WIFI_PORTAL_TIMEOUT_SEC 180   // give up and reboot after this long with no setup
