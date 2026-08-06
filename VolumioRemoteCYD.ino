/* CYD 2.8" - ESP32-2432S028R DEV Module
   Driver ST7789
   XPT2046 touch on its own separate SPI bus.
   NO OTA 2MB APP/2MB SPIFFS
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include "config.h"
#include "volumio_api.h"
#include "artwork.h"
#include "settings.h"
#include "webui.h"
#include "Language.h"
#include "Translation.h"

TFT_eSPI tft = TFT_eSPI();

struct ButtonRect {
  int x, y, w, h;
};

struct TitleParts {
  String artist;
  String track;
  String album;
};


static const int HEADER_Y = 2;// Station/album name header - centered

static const int HEADER_SIDE_RESERVED = 40;
static const int HEADER_TEXT_MAXW = SCREEN_W - 2 * HEADER_SIDE_RESERVED;  // 160
static const int HEADER_CLEAR_X0 = HEADER_SIDE_RESERVED;                  // 40

static const int TEXT_X = 10;
static const int TEXT_W = SCREEN_W - 2 * TEXT_X;

static const int TEXT_BLOCK_H = Y_PROGRESS_BAR - Y_ARTIST;
static const int TRACK_INDENT = 16;  // how far the track (2nd) line indents under the artist line

static const int PROGRESS_H = 8;

static const int VOL_NUMBER_X = SCREEN_W - TEXT_X - 40;
static const int VOL_NUMBER_CLEAR_W = 40;
static const int VOL_TEXT_CLEAR_H = 20;
static bool volLabelDrawn = false;

// "Source: ..." now shares the Vol row instead of its own line above the
// progress bar - one less line competing for the tight space above
// Y_PROGRESS_BAR. 30 = the "Vol" label's real rendered width (23px at the
// small font) plus a small gap before it, measured from the font file
// rather than guessed.
static const int SOURCE_MAXW = VOL_NUMBER_X - TEXT_X - 30;

static const int BTN_ICON_SRC = 34;   // native size of every button JPEG in data/
static const int BTN_ICON_SIZE = 34;  // on-screen size, bottom row - back to matching BTN_ICON_SRC (1:1, no resample)
static const int BTN_MARGIN = 3;      // left/right edge margin
static const int BTN_GAP = 4;         // gap within a group
static const int BTN_GROUP_GAP = 12;  // extra gap between the two groups
static const int BTN_Y = Y_BUTTONS_BOTTOM - BTN_ICON_SIZE;  // top of the icons - Y_BUTTONS_BOTTOM (config.h) is the tunable

static const int BTN_X0 = BTN_MARGIN;
static const int BTN_X1 = BTN_X0 + BTN_ICON_SIZE + BTN_GAP;
static const int BTN_X2 = BTN_X1 + BTN_ICON_SIZE + BTN_GAP;
static const int BTN_X3 = BTN_X2 + BTN_ICON_SIZE + BTN_GROUP_GAP;
static const int BTN_X4 = BTN_X3 + BTN_ICON_SIZE + BTN_GAP;
static const int BTN_X5 = BTN_X4 + BTN_ICON_SIZE + BTN_GAP;

static ButtonRect btnRewind = { BTN_X0, BTN_Y, BTN_ICON_SIZE, BTN_ICON_SIZE };
static ButtonRect btnPlay = { BTN_X1, BTN_Y, BTN_ICON_SIZE, BTN_ICON_SIZE };
static ButtonRect btnForward = { BTN_X2, BTN_Y, BTN_ICON_SIZE, BTN_ICON_SIZE };
static ButtonRect btnVolDown = { BTN_X3, BTN_Y, BTN_ICON_SIZE, BTN_ICON_SIZE };
static ButtonRect btnVolOff = { BTN_X4, BTN_Y, BTN_ICON_SIZE, BTN_ICON_SIZE };
static ButtonRect btnVolUp = { BTN_X5, BTN_Y, BTN_ICON_SIZE, BTN_ICON_SIZE };

// Station-list button lives in the top-left corner instead of the bottom
// row - see HEADER_SIDE_RESERVED near drawHeader() for the matching
// clear-zone that keeps this corner (and the WiFi bars' corner) untouched
// by header repaints. Sized smaller (24px) than the row buttons, which is
// why the draw calls below key off each button's own rect.w/h rather than
// the shared BTN_ICON_SIZE constant.
static const int STATION_ICON_SIZE = 24;
static const int STATION_ICON_X = 2;
static const int STATION_ICON_Y = 2;
static ButtonRect btnStationList = { STATION_ICON_X, STATION_ICON_Y, STATION_ICON_SIZE, STATION_ICON_SIZE };

struct IconButton {
  ButtonRect rect;
  const char *iconPath;
  const char *command;
};


static IconButton kButtons[] = {
  { btnRewind, buttonRewind_IMAGE, "prev" },
  { btnPlay, buttonPlay_IMAGE, "toggle" },
  { btnForward, buttonForward_IMAGE, "next" },
  { btnVolDown, buttonVolDown_IMAGE, "volume&volume=minus" },
  { btnVolOff, buttonVolOff_IMAGE, nullptr },
  { btnVolUp, buttonVolUp_IMAGE, "volume&volume=plus" },
  { btnStationList, buttonStationList_IMAGE, nullptr },
};
static const int kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);
static const int kVolOffIndex = 4;       // index of the vol-off button in kButtons, above
static const int kPlayIndex = 1;         // index of the play/stop toggle button in kButtons, above
static const int kStationListIndex = 6;  // index of the station-list button in kButtons, above

static const int kTransportButtonCount = 3;
static bool volumeMuted = false;
static bool localPlaying = false;
static int pressedButtonIndex = -1;

static const char *iconPathFor(int index) {
  if (index == kPlayIndex) return localPlaying ? buttonStop_IMAGE : buttonPlay_IMAGE;
  return kButtons[index].iconPath;
}

static bool transportButtonsVisible = true;

static void setTransportButtonsVisible(bool visible) {
  if (visible) {
    for (int i = 0; i < kTransportButtonCount; i++) {
      artworkDrawIcon(iconPathFor(i), kButtons[i].rect.x, kButtons[i].rect.y, BTN_ICON_SRC, BTN_ICON_SIZE);
    }
  } else {
    int groupX = kButtons[0].rect.x;
    int groupW = (kButtons[kTransportButtonCount - 1].rect.x + BTN_ICON_SIZE) - groupX;
    tft.fillRect(groupX, BTN_Y, groupW, BTN_ICON_SIZE, TFT_BLACK);
  }
  transportButtonsVisible = visible;
}

static String lastTitle = "";
static String lastAlbumArtUrl = "";

// Touch load/save/clear/calibrate live in touch_cyd.cpp on this board -
// see the comment there for why (separate SPI bus from the display, so
// TFT_eSPI's built-in touch support doesn't apply here).
static void displayInit();                                                                                          // one-time boot setup: load/run cal, blank screen, draw buttons
static void drawWrapped(const String &msg, int x, int y, int w, int font, uint16_t color);                          // multi-line word-wrapped text
static void drawTruncated(const String &msg, int x, int y, int maxWidth, int font, uint16_t color, uint8_t datum);  // single line, "..." if it overflows maxWidth
static int measuredTextWidth(const String &msg, int font);                                                           // width in px if drawn at `font`, without drawing it
static int measuredFontHeight(int font);                                                                             // rendered line height of `font`, without drawing anything
static void displayShowStatus(const String &msg, bool fullClear = false);                                           // boot/connection status message (reuses art+text area); fullClear=true wipes the whole screen instead (needed when overlaid on the full-height station-list screen)
static void drawHeader(const VolumioState &state);                                                                  // artist line, top of screen
static void drawTextBlock(const VolumioState &state);                                                               // track/album/source lines below the art
static void displayShowTrack(const VolumioState &state, bool artChanged);                                           // redraws header/text/art, but only what actually changed
static void displayUpdateProgress(const VolumioState &state);                                                       // progress bar (library/AirPlay) or volume slider (webradio)
void drawWiFiQuality();                                                                                             // signal-strength bars, top right
int8_t getWifiQuality();                                                                                            // RSSI dBm -> 0-100 signal quality

// Station-list overlay (favorite web radio switcher) - see the block above
// touchPoll() for the implementations. redrawMainScreen() is the shared
// "force everything to repaint" tail used both here and by exitScreensaver().
static void redrawMainScreen();
static void openStationList();
static void closeStationList();
static void drawStationList();
static void stationListTouch(bool touched, uint16_t tx, uint16_t ty);
static void openFavoritesList();               // webradio Favorite Radios - entry point when no USB is mounted, or chosen from the chooser
static void openUsbFolderList();                // enters USB browsing at the drive root
static void browseUsbCurrentLevel();            // (re)fetches and shows whatever USB folder/level usbStackDepth currently points at
static void usbBrowseGoBack();                  // pops one level of the USB nav stack (or returns to the chooser at the root)
static void playUsbSongAt(int idx);             // plays the song at row idx, queuing the other songs at this same level
static int slRowCount();                        // how many rows the current mode has
static String slRowName(int idx);               // row idx's display name in the current mode
static void slGoBack();                         // header "< Back" tap - no-op at a true top level
static void slRowTapped(int idx);               // row (text area) tap - meaning depends on slMode
static void slRowPlayTapped(int idx);           // row's Play icon tap - always plays immediately, never navigates

static bool inRect(int tx, int ty, ButtonRect r);  // hit-test a touch point against a padded button rect
static void touchPoll();                           // reads touch, dispatches slider drag or button press/release
static uint32_t lastTouchMs = 0;                   // millis() of last dispatched button press, for debounce below
static const uint32_t TOUCH_DEBOUNCE_MS = 300;     // min gap between button presses (contact-bounce guard)

// Throttles the "Touch at (x, y)" debug print only - unrelated to the
// per-button debounce above, which only governs command dispatch.
static uint32_t lastTouchLogMs = 0;                 // millis() of last debug print
static const uint32_t TOUCH_LOG_INTERVAL_MS = 250;  // min gap between debug prints

static WiFiManager wm;      // captive-portal WiFi provisioning
static void connectWifi();  // connect with saved creds, or launch the AP portal if none/failed

static VolumioState currentState;  // last-polled playback state, shared across loop()/touchPoll()
static uint32_t lastPollMs = 0;    // millis() of last getState poll, paces the poll interval

static uint32_t lastWifiQualityMs = 0;                   // millis() of last signal-bars redraw
static const uint32_t WIFI_QUALITY_INTERVAL_MS = 60000;  // redraw at most once a minute - RSSI doesn't need to be live

static bool screensaverActive = false;
static uint32_t lastInteractionMs = 0;  // millis() of last touch - drives the idle timer
static void enterScreensaver();
static void exitScreensaver();
static void screensaverAnimate();

// ---------------------------------------------------------------------
// Station-list overlay state - see drawStationList()/stationListTouch()
// ---------------------------------------------------------------------
static bool stationListActive = false;
static WebRadioStation stationList[MAX_STATIONS];
static int stationCount = 0;
static int stationScrollOffset = 0;  // index of the first visible row (pages by SL_VISIBLE_ROWS)
static bool slWasTouched = false;    // edge-detect: fire on press only, same pattern as the main button row

static const int SL_HEADER_H = 34;
static const int SL_FOOTER_H = 34;
static const int SL_VISIBLE_ROWS = 7;
static const int SL_ROW_H = (SCREEN_H - SL_HEADER_H - SL_FOOTER_H) / SL_VISIBLE_ROWS;  // 36 - divides evenly at 240x320
static const int SL_LIST_Y0 = SL_HEADER_H;
static const int SL_FOOTER_Y = SCREEN_H - SL_FOOTER_H;

// Per-row Play icon (Favorites/USB browsing only - not the chooser, whose
// 2 rows are navigation, not playable content). Mirrors Volumio's own web
// UI: tapping the row itself keeps its existing meaning (open a folder,
// play a song), tapping this icon always plays immediately - for a folder,
// that means everything nested inside it, without having to open it first.
// Leading position (first item on the row, left of the title) to match
// Volumio's own layout.
static const int SL_PLAY_ICON_SRC = 32;   // native size of stationPlayIcon.jpg in data/
static const int SL_PLAY_ICON_SIZE = 24;  // on-screen size, vertically centered in the 36px row
static const int SL_PLAY_ICON_X = TEXT_X;                              // left-aligned - first item on the row
static const int SL_ROW_TEXT_X = SL_PLAY_ICON_X + SL_PLAY_ICON_SIZE + 8;  // title starts after the icon + a gap
static const int SL_ROW_TEXT_MAXW = SCREEN_W - SL_ROW_TEXT_X - TEXT_X;    // same right margin as before, just shifted over

// SL_MODE_USB_BROWSE covers every USB level (top-level folders, and any
// folder nested underneath them) - replaces the old fixed two-level
// SL_MODE_USB_FOLDERS/SL_MODE_USB_SONGS pair, which only handled drives laid
// out as "USB/<folder>/<song>.mp3" and silently found nothing on the far
// more common "USB/<artist>/<album>/<song>.mp3" layout.
enum StationListMode { SL_MODE_FAVORITES, SL_MODE_CHOOSER, SL_MODE_USB_BROWSE };
static StationListMode slMode = SL_MODE_FAVORITES;
static bool slCameFromChooser = false;  // true only if favorites was reached via the chooser (vs. auto-skip when no USB is mounted) - controls whether a back arrow shows
static String slCurrentFolderName;  // for the "< A" / "< B" back-button label while viewing a folder's songs

// USB navigation stack - usbStackUri[0..usbStackDepth] is the path of URIs
// from the drive root down to whatever level is currently on screen, so
// "< Back" can pop out one folder at a time no matter how deep the drive's
// actual folder structure goes (unlike the old 2-level-only model).
static const int USB_STACK_MAX = 8;  // e.g. USB/Artist/Album/Disc/... - plenty for any real music library layout
static String usbStackUri[USB_STACK_MAX];
static String usbStackName[USB_STACK_MAX];
static int usbStackDepth = 0;

// ---------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS init failed!");
    while (1) yield();
  }
  Serial.println("LittleFS ready.");

  settingsInit();  // Discogs token / iTunes toggle - independent of WiFi/display, safe this early

  displayInit();// setup in portrait mode
  connectWifi(); // connect to wifi
  webuiInit();  // settings web page - needs WiFi up for mDNS/IP
  displayShowStatus(strWaitingForVolumio);

  drawWiFiQuality();  // draw once from cold boot; loop() re-draws on its own 1-minute interval from here
  lastWifiQualityMs = millis();
  lastInteractionMs = millis();  // idle clock for the screensaver starts now, not at millis()==0
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
    return;
  }

  webuiHandleClient();

  uint32_t now = millis();

  if (!screensaverActive && !stationListActive && now - lastWifiQualityMs >= WIFI_QUALITY_INTERVAL_MS) {
    lastWifiQualityMs = now;
    drawWiFiQuality();// show the wifi signal strength
  }

  // Slower poll cadence while the screensaver is up - see
  // SCREENSAVER_POLL_INTERVAL_MS in config.h.
  uint32_t effectivePollInterval = screensaverActive ? SCREENSAVER_POLL_INTERVAL_MS : POLL_INTERVAL_MS;
  if (now - lastPollMs >= effectivePollInterval) {
    lastPollMs = now;
    VolumioState newState;
    bool pollOk = volumioGetState(newState);
    if (pollOk) {
      bool artChanged = (newState.albumArtUrl != currentState.albumArtUrl) || (newState.title != currentState.title);
      bool wasPlaying = (currentState.status == "play");
      currentState = newState;
      bool nowPlaying = (currentState.status == "play");


      if (nowPlaying) {
        lastInteractionMs = now;
      }

  
      if (screensaverActive && nowPlaying != wasPlaying) {
        exitScreensaver();
      }

      if (screensaverActive || stationListActive) {
        // Keep state in sync without touching the screen - the station-list
        // overlay owns the display while it's up, same as the screensaver.
        transportButtonsVisible = !currentState.disableUiControls;
        volumeMuted = currentState.mute;
        localPlaying = nowPlaying;
      } else {
        // Transport controls hide during AirPlay - source device controls
        // play/pause/skip, only volume is adjustable from here.
        bool shouldShowTransport = !currentState.disableUiControls;
        if (shouldShowTransport != transportButtonsVisible) {
          setTransportButtonsVisible(shouldShowTransport);
        }

        volumeMuted = currentState.mute;

        // Resync play/stop icon - catches playback started/stopped elsewhere.
        if (nowPlaying != localPlaying) {
          localPlaying = nowPlaying;
          if (transportButtonsVisible && pressedButtonIndex != kPlayIndex) {
            artworkDrawIcon(iconPathFor(kPlayIndex), btnPlay.x, btnPlay.y, BTN_ICON_SRC, BTN_ICON_SIZE);
          }
        }

        displayShowTrack(currentState, artChanged);
        displayUpdateProgress(currentState);
      }
    } else if (!screensaverActive) {
      displayShowStatus(String(strVolumioUnreachable) + settingsGetVolumioHost());
    }
  }

  now = millis();
  uint32_t ssTimeoutMin = settingsGetScreensaverTimeoutMin();
  if (!screensaverActive && !stationListActive && ssTimeoutMin > 0 && (now - lastInteractionMs) >= ssTimeoutMin * 60000UL) {
    enterScreensaver();
  }
  if (screensaverActive) {
    screensaverAnimate();
  }

  touchPoll();
}

// ---------------------------------------------------------------------
// Display: track/album/art rendering
// ---------------------------------------------------------------------

// Redraws all seven button icons (six-button row + the corner station-list
// icon) - used at boot and after the screensaver/station-list overlay.
// Per-button rect.w rather than the shared BTN_ICON_SIZE - the corner icon
// is a different size (24px) than the row (34px).
static void drawAllButtonIcons() {
  for (int i = 0; i < kButtonCount; i++) {
    artworkDrawIcon(iconPathFor(i), kButtons[i].rect.x, kButtons[i].rect.y, BTN_ICON_SRC, kButtons[i].rect.w);
  }
}

static void displayInit() {
  tft.init();

  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  touchCydInit();
  if (!touchCydLoadCalibration()) {
    touchCydCalibrate(tft);
  }

  tft.fillScreen(TFT_BLACK);
  artworkInit();
  drawAllButtonIcons();
}

static void displayShowStatus(const String &msg, bool fullClear) {
  if (fullClear) {
    // Used when this status message is shown on top of the station-list
    // overlay (favorites/USB browsing), which paints all the way down to
    // SL_FOOTER_Y near the bottom of the screen - the partial clear below
    // only wipes the main-screen's art/text area and leaves the tail end
    // of that taller screen (e.g. the last folder row) on screen underneath.
    tft.fillScreen(TFT_BLACK);
    drawWrapped(msg, TEXT_X, ART_Y, SCREEN_W - 2 * TEXT_X, 2, TFT_WHITE);
    return;
  }
  int areaH = Y_ARTIST + TEXT_BLOCK_H;
  tft.fillRect(HEADER_CLEAR_X0, 0, HEADER_TEXT_MAXW, ART_Y, TFT_BLACK);
  tft.fillRect(0, ART_Y, SCREEN_W, areaH - ART_Y, TFT_BLACK);
  drawWrapped(msg, TEXT_X, ART_Y, SCREEN_W - 2 * TEXT_X, 2, TFT_WHITE);
}

// Artist, top of screen, centered, truncated to one line.
static void drawHeader(const VolumioState &state) {
  // No fillRect here - the caller (displayShowTrack()) already did a scoped
  // clear of just this row's center strip, leaving both top corners intact.
  drawTruncated(state.artist, SCREEN_W / 2, HEADER_Y, HEADER_TEXT_MAXW, 2, TFT_LIGHTGREY, TC_DATUM);
}

// Reorders a "Last, First" artist string to natural "First Last" order -
// see artwork.cpp's normalizeArtistName() (same logic, kept in sync).
static String normalizeArtistName(const String &artist) {
  int comma = artist.indexOf(',');
  if (comma < 0) return artist;
  if (artist.indexOf(',', comma + 1) >= 0) return artist;  // more than one comma isn't this pattern

  String last = artist.substring(0, comma);
  String first = artist.substring(comma + 1);
  last.trim();
  first.trim();
  return first + " " + last;
}

// Splits "Artist - Track - Album" on " - ", also handles "Last, First,
// Track". Keep in sync with artwork.cpp's splitArtistTitle().
static TitleParts splitTitle(const String &combined) {
  TitleParts parts;

  int firstSep = combined.indexOf(" - ");
  if (firstSep < 0) {
    int firstComma = combined.indexOf(',');
    int secondComma = firstComma >= 0 ? combined.indexOf(',', firstComma + 1) : -1;
    if (firstComma >= 0 && secondComma >= 0 && combined.indexOf(',', secondComma + 1) < 0) {
      String last = combined.substring(0, firstComma);
      String first = combined.substring(firstComma + 1, secondComma);
      last.trim();
      first.trim();
      parts.artist = first + " " + last;
      parts.track = combined.substring(secondComma + 1);
      parts.track.trim();
      return parts;
    }

    parts.track = combined;  // no recognizable pattern - show it as-is on the track line
    return parts;
  }

  parts.artist = normalizeArtistName(combined.substring(0, firstSep));
  String rest = combined.substring(firstSep + 3);

  int secondSep = rest.indexOf(" - ");
  if (secondSep < 0) {
    parts.track = rest;
  } else {
    parts.track = rest.substring(0, secondSep);
    parts.album = rest.substring(secondSep + 3);
  }
  return parts;
}

static void drawTextBlock(const VolumioState &state) {
  tft.fillRect(TEXT_X, Y_ARTIST, TEXT_W, TEXT_BLOCK_H, TFT_BLACK);

  TitleParts parts = splitTitle(state.title.length() ? state.title : "(no title)");

  // splitTitle() only populates parts.artist for webradio's "Artist -
  // Track" ICY metadata; for mpd/library and AirPlay, state.artist is
  // already shown up in the header (drawHeader()), so parts.artist comes
  // back empty here and that line is skipped entirely below.
  bool hasArtistLine = parts.artist.length() > 0;

  // Prefer Volumio's own `album` field (AirPlay/library) over splitTitle()'s
  // webradio-only parse.
  String album = state.album.length() ? state.album : parts.album;
  bool hasAlbumLine = album.length() > 0;

  // Step 1 - what fits: only the artist line is ever a fit test now. Track
  // and album are always the small font (2) - a fixed hierarchy, not
  // something to test - artist is the only line worth spending the large
  // font on when it fits, given how little vertical room this panel has.
  int artistFont = hasArtistLine ? ((measuredTextWidth(parts.artist, 4) <= TEXT_W) ? 4 : 2) : 2;
  int trackMaxW = TEXT_W - TRACK_INDENT;
  const int trackFont = 2;
  const int albumFont = 2;

  // Step 2 - flag each font for its real size: measure the actual rendered
  // height of whatever font each line landed on above - the font's real
  // height, whichever of the two sizes it turned out to be, not a guessed
  // constant. Track/album share one measurement since they're always the
  // same (small) font.
  int hArtist = hasArtistLine ? measuredFontHeight(artistFont) : 0;
  int hSmall = measuredFontHeight(trackFont);
  int hTrack = hSmall;
  int hAlbum = hasAlbumLine ? hSmall : 0;

  // Step 3 - divide the area: lay out whichever lines are active
  // (artist/track/album) top to bottom starting at Y_ARTIST, splitting
  // whatever space is left after their real heights evenly between them as
  // gaps, all the way down to Y_PROGRESS_BAR - works for any combination of
  // line heights instead of a fixed per-pair-of-lines guess.
  //
  // Source used to be a 4th line in this same distribution, but three
  // lines' worth of real heights already came close to overrunning
  // Y_PROGRESS_BAR (see Step 2's real numbers: 33+20+20 = 73 of the 81px
  // available) - adding Source's own 20px on top of that regularly pushed
  // it past Y_PROGRESS_BAR, where the progress bar's own redraw then
  // painted over it. Source now shares the Vol row below instead (see the
  // end of this function) - one less line competing for this space, and
  // three lines fit this zone comfortably on their own.
  //
  // No separate "does this fit vertically" check needed before laying these
  // three out: each line's Y below is the previous line's own real bottom
  // edge (+ gap), so lines can never overlap each other no matter which
  // fonts they land on - gap simply floors at 0 (see max() below) if the
  // real content is taller than the available space, rather than a line's
  // fixed position being blind to what rendered above it (that
  // fixed-position blindness was the actual cause of the original
  // track/album overlap bug).
  int zoneTop = Y_ARTIST;
  int zoneH = TEXT_BLOCK_H;  // = Y_PROGRESS_BAR - Y_ARTIST (config.h)
  int lineCount = (hasArtistLine ? 1 : 0) + 1 /* track */ + (hasAlbumLine ? 1 : 0);
  int gapCount = lineCount - 1;
  int contentH = hArtist + hTrack + hAlbum;
  int gap = gapCount > 0 ? max(0, (zoneH - contentH) / gapCount) : 0;

  int y = zoneTop;
  if (hasArtistLine) {
    drawTruncated(parts.artist, TEXT_X, y, TEXT_W, artistFont, TFT_WHITE, TL_DATUM);
    y += hArtist + gap;
  }

  drawTruncated(parts.track, TEXT_X + TRACK_INDENT, y, trackMaxW, trackFont, TFT_WHITE, TL_DATUM);
  y += hTrack + gap;

  if (hasAlbumLine) {
    // Pulled up 4px off its computed slot - manual tweak for a bit more
    // breathing room between album and the progress bar below (album is
    // always the last line drawn here, so this only affects the gap after
    // it, not the artist->track or track->album spacing above).
    drawTruncated(album, TEXT_X, y - 4, TEXT_W, albumFont, TFT_LIGHTGREY, TL_DATUM);
  }

  // Source moved down to share the Vol row instead of taking its own line
  // above the progress bar - "Vol NN" is right-anchored at VOL_NUMBER_X and
  // only ~30px wide (see SOURCE_MAXW above), leaving the whole left side of
  // that row free. Cleared and redrawn here (on track change) rather than
  // in displayUpdateProgress() (which only touches the Vol number itself,
  // on volume change) since this needs to update on a different trigger.
  tft.fillRect(TEXT_X, Y_VOL_NUMBER - 3, SOURCE_MAXW, VOL_TEXT_CLEAR_H, TFT_BLACK);
  drawTruncated("Source: " + state.service, TEXT_X, Y_VOL_NUMBER, SOURCE_MAXW, 2, TFT_DARKGREY, TL_DATUM);
}

static void displayShowTrack(const VolumioState &state, bool artChanged) {
  bool trackChanged = (state.title != lastTitle);

  if (trackChanged) {

    tft.fillRect(HEADER_CLEAR_X0, 0, HEADER_TEXT_MAXW, ART_Y, TFT_BLACK);
    tft.fillRect(0, ART_Y, SCREEN_W, (Y_ARTIST + TEXT_BLOCK_H) - ART_Y, TFT_BLACK);
    drawHeader(state);
    drawTextBlock(state);
    lastTitle = state.title;
  }

  if (artChanged || state.albumArtUrl != lastAlbumArtUrl) {
    artworkRender(state, ART_X, ART_Y, ART_SIZE);
    lastAlbumArtUrl = state.albumArtUrl;
  }
}

static int lastProgressMode = -1;         // -1 = never drawn, 0 = webradio, 1 = library/other
static int lastDrawnVolume = -1;          // drives both the webradio slider fill and the "Vol NN" text (both modes)
static int lastDrawnProgressFillPx = -1;  // drives the library/flashdrive progress fill only

static void displayUpdateProgress(const VolumioState &state) {
  bool isWebradio = (state.service == "webradio");
  int mode = isWebradio ? 0 : 1;
  int vol = constrain(state.volume, 0, 100);

  int barX = TEXT_X;
  int barY = Y_PROGRESS_BAR;
  int barW = TEXT_W;
  int barH = PROGRESS_H;

  int fillW = 0;
  if (isWebradio) {
    // No fixed duration for a live stream - bar repurposed as a volume slider.
    fillW = (barW - 2) * vol / 100;
  } else if (state.duration > 0) {
    float frac = constrain((float)state.seek / 1000.0f / (float)state.duration, 0.0f, 1.0f);
    fillW = (int)(frac * (barW - 2));
  }

  bool modeChanged = (mode != lastProgressMode);
  bool volumeChanged = (vol != lastDrawnVolume);
  bool progressChanged = isWebradio ? false : (fillW != lastDrawnProgressFillPx);

  // Bar and text redraw independently - a progress bar advancing every poll
  // shouldn't also flash the volume text when the volume hasn't changed.
  bool barNeedsRedraw = modeChanged || (isWebradio ? volumeChanged : progressChanged);
  bool textNeedsRedraw = volumeChanged;

  if (!barNeedsRedraw && !textNeedsRedraw) return;

  if (barNeedsRedraw) {
    uint16_t fillColor = isWebradio ? TFT_CYAN : TFT_GREEN;
    tft.drawRect(barX, barY, barW, barH, TFT_DARKGREY);  // re-stroked every time so it self-heals
    if (modeChanged) {
      tft.fillRect(barX + 1, barY + 1, barW - 2, barH - 2, TFT_BLACK);
      if (fillW > 0) {
        tft.fillRect(barX + 1, barY + 1, fillW, barH - 2, fillColor);
      }
    } else {
      // Same mode as last draw - paint just the delta sliver instead of
      // redrawing the whole bar.
      int prevFillW = lastDrawnProgressFillPx;
      if (fillW > prevFillW) {
        tft.fillRect(barX + 1 + prevFillW, barY + 1, fillW - prevFillW, barH - 2, fillColor);
      } else if (fillW < prevFillW) {
        tft.fillRect(barX + 1 + fillW, barY + 1, prevFillW - fillW, barH - 2, TFT_BLACK);
      }
    }
    lastProgressMode = mode;
    lastDrawnProgressFillPx = fillW;
  }

  if (textNeedsRedraw) {
    tft.loadFont(AA_FONT_SMALL, LittleFS);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    if (!volLabelDrawn) {
      // One-time draw, right-anchored at VOL_NUMBER_X.
      tft.setTextDatum(TR_DATUM);
      tft.drawString(strVol, VOL_NUMBER_X, Y_VOL_NUMBER, 2);
      volLabelDrawn = true;
    }

    tft.fillRect(VOL_NUMBER_X, Y_VOL_NUMBER - 3, VOL_NUMBER_CLEAR_W, VOL_TEXT_CLEAR_H, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(String(state.volume >= 0 ? state.volume : 0), VOL_NUMBER_X, Y_VOL_NUMBER, 2);
    tft.unloadFont();
    lastDrawnVolume = vol;
  }
}


static const char *fontFileFor(int font) {
  return (font == 4) ? AA_FONT_LARGE : AA_FONT_SMALL;
}


static int measuredTextWidth(const String &msg, int font) {
  String text = sanitizeForFont(msg);
  tft.loadFont(fontFileFor(font), LittleFS);
  int w = tft.textWidth(text, font);
  tft.unloadFont();
  return w;
}

// Rendered line height of `font` - used by drawTextBlock() to lay out the
// artist/track/album lines by their real measured height instead of a
// guessed constant.
static int measuredFontHeight(int font) {
  tft.loadFont(fontFileFor(font), LittleFS);
  int h = tft.fontHeight(font);
  tft.unloadFont();
  return h;
}


static String sanitizeForFont(const String &in) {
  String out;
  out.reserve(in.length());
  size_t i = 0, len = in.length();
  while (i < len) {
    uint8_t b0 = (uint8_t)in[i];
    // 3-byte UTF-8 sequences in the E2 80 xx range (General Punctuation,
    // U+2018-U+2026).
    if (b0 == 0xE2 && i + 2 < len && (uint8_t)in[i + 1] == 0x80) {
      uint8_t b2 = (uint8_t)in[i + 2];
      switch (b2) {
        case 0x98:
        case 0x99:
          out += '\'';
          i += 3;
          continue;  // ' ' -> '
        case 0x9C:
        case 0x9D:
          out += '"';
          i += 3;
          continue;  // " " -> "
        case 0x93:
          out += '-';
          i += 3;
          continue;  // en dash -> -
        case 0xA6:
          out += "...";
          i += 3;
          continue;      // … -> ...
        default: break;  // includes em dash (0x94) - font already has it
      }
    }
    out += (char)b0;
    i++;
  }
  return out;
}

// Greedy word-wrap for status messages (WiFi portal instructions etc.) that
// are too long to fit a single line at this width.
static void drawWrapped(const String &msg, int x, int y, int w, int font, uint16_t color) {
  String text = sanitizeForFont(msg);
  tft.loadFont(fontFileFor(font), LittleFS);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color, TFT_BLACK);
  int lineHeight = tft.fontHeight(font) + 4;
  int cy = y;

  int start = 0;
  int len = text.length();
  while (start < len) {
    int end = len;
    while (end > start && tft.textWidth(text.substring(start, end), font) > w) {
      int lastSpace = text.lastIndexOf(' ', end - 1);
      if (lastSpace > start) end = lastSpace;
      else { end--; }  // no space to break on - hard cut
    }
    if (end <= start) end = start + 1;  // safety net

    tft.drawString(text.substring(start, end), x, cy, font);
    cy += lineHeight;

    start = end;
    while (start < len && text[start] == ' ') start++;  // skip the space we broke on
  }
  tft.unloadFont();
}

// Single-line field, truncated with a trailing "..." if it overflows maxWidth.
static void drawTruncated(const String &msg, int x, int y, int maxWidth, int font, uint16_t color, uint8_t datum) {
  String text = sanitizeForFont(msg);
  tft.loadFont(fontFileFor(font), LittleFS);
  tft.setTextDatum(datum);
  tft.setTextColor(color, TFT_BLACK);

  int safeMaxWidth = maxWidth - 4;

  if (tft.textWidth(text, font) <= safeMaxWidth) {
    tft.drawString(text, x, y, font);
    tft.unloadFont();
    return;
  }

  String truncated = text;
  int ellipsisW = tft.textWidth("...", font);
  while (truncated.length() > 0 && tft.textWidth(truncated, font) + ellipsisW > safeMaxWidth) {
    truncated.remove(truncated.length() - 1);
  }
  tft.drawString(truncated + "...", x, y, font);
  tft.unloadFont();
}

// ---------------------------------------------------------------------
// Station-list overlay - favorite web radio switcher. Opened from the
// station-list button (kStationListIndex, in kButtons[] above); takes over
// the whole screen and all touch input until closed, same pattern as the
// screensaver.
// ---------------------------------------------------------------------

static void openStationList() {
  pressedButtonIndex = -1;
  displayShowStatus(strStationsLoading, true);

  // Cheap presence check only (one browse call at the drive root) - the
  // actual folder listing is deferred until the user picks "USB Music" in
  // the chooser below, so this doesn't pay for a browse fetch that might
  // not even be used (e.g. user picks Web Radio Favorites instead).
  String usbRootUri;
  bool hasUsb = volumioGetUsbRootUri(usbRootUri);

  if (hasUsb) {
    slMode = SL_MODE_CHOOSER;
    stationScrollOffset = 0;
    slWasTouched = true;  // finger is still down from the tap that opened this screen - don't let it register as a row/close tap too
    stationListActive = true;
    drawStationList();
    return;
  }

  slCameFromChooser = false;
  openFavoritesList();
}

static void closeStationList() {
  stationListActive = false;
  pressedButtonIndex = -1;
  redrawMainScreen();
}

// Favorite Radios list - unchanged behavior from before USB support existed,
// just factored out so both openStationList() (no USB) and the chooser (USB
// present, "Web Radio Favorites" tapped) can reach it.
static void openFavoritesList() {
  displayShowStatus(strStationsLoading, true);
  stationCount = volumioGetFavouriteWebRadios(stationList, MAX_STATIONS);

  if (stationCount <= 0) {
    displayShowStatus(strNoStations, true);
    delay(1200);
    redrawMainScreen();
    return;
  }

  slMode = SL_MODE_FAVORITES;
  stationScrollOffset = 0;
  slWasTouched = true;
  stationListActive = true;
  drawStationList();
}

// Entry point from the chooser - starts a fresh USB browse session at the
// drive root (depth 0) and fetches its first level.
static void openUsbFolderList() {
  usbStackDepth = 0;
  String rootUri;
  if (!volumioGetUsbRootUri(rootUri)) {
    // Drive was unplugged between the chooser's presence check and this tap -
    // bail back out to the chooser rather than showing a broken empty list.
    slMode = SL_MODE_CHOOSER;
    stationScrollOffset = 0;
    drawStationList();
    return;
  }
  usbStackUri[0] = rootUri;
  usbStackName[0] = strUsbMusicTitle;
  browseUsbCurrentLevel();
}

// Fetches and shows whatever USB level usbStackDepth currently points at -
// called both when drilling into a folder and when backing out of one.
// Falls back a level (or to the chooser, at the root) if that level turns
// out to have nothing playable in it directly - same spirit as
// openFavoritesList()'s empty-result handling, but with its own wording
// (strUsbFolderEmpty) rather than reusing the web-radio "no favorites"
// message, which was misleading here.
static void browseUsbCurrentLevel() {
  displayShowStatus(strUsbLoading, true);
  stationCount = volumioBrowseUsb(usbStackUri[usbStackDepth], stationList, MAX_STATIONS);
  slCurrentFolderName = usbStackName[usbStackDepth];

  if (stationCount <= 0) {
    displayShowStatus(strUsbFolderEmpty, true);
    delay(1200);
    usbBrowseGoBack();
    return;
  }

  slMode = SL_MODE_USB_BROWSE;
  stationScrollOffset = 0;
  slWasTouched = true;
  stationListActive = true;
  drawStationList();
}

static void usbBrowseGoBack() {
  if (usbStackDepth > 0) {
    usbStackDepth--;
    browseUsbCurrentLevel();
  } else {
    slMode = SL_MODE_CHOOSER;
    stationScrollOffset = 0;
    drawStationList();
  }
}

// Builds the play queue from just the song rows at the current USB level
// (skipping any folder rows mixed in alongside them - a level can have
// both, e.g. loose tracks sitting next to album subfolders) and starts
// playback at the tapped one.
static void playUsbSongAt(int idx) {
  static WebRadioStation songsOnly[MAX_STATIONS];
  int songCount = 0;
  int startIndex = -1;
  for (int i = 0; i < stationCount; i++) {
    if (stationList[i].isFolder) continue;
    if (i == idx) startIndex = songCount;
    songsOnly[songCount++] = stationList[i];
  }
  if (startIndex < 0) return;  // shouldn't happen - idx was a folder row, not a song row

  closeStationList();
  volumioPlayUsbSong(songsOnly, songCount, startIndex);
}

// Returns how many rows the CURRENT mode has, and where their names come
// from - the one thing that genuinely differs per screen. Used by both
// drawStationList() and stationListTouch() so the two can't disagree.
static int slRowCount() {
  switch (slMode) {
    case SL_MODE_CHOOSER: return 2;
    default:              return stationCount;  // SL_MODE_FAVORITES or SL_MODE_USB_BROWSE
  }
}

static String slRowName(int idx) {
  switch (slMode) {
    case SL_MODE_CHOOSER: return String(idx == 0 ? strChoiceWebRadio : strChoiceUsbMusic);
    default:
      // Trailing "/" marks folder rows in USB browsing so it's clear which
      // rows drill deeper vs. which ones play a song.
      if (slMode == SL_MODE_USB_BROWSE && stationList[idx].isFolder) return stationList[idx].name + " /";
      return stationList[idx].name;
  }
}

// Full-screen redraw: header (title, doubling as "< Back" when not at the
// true top level for this session) + close "X", a page of rows, and
// Prev/Next paging in the footer if there are more rows than fit on one
// page (SL_VISIBLE_ROWS at a time).
static void drawStationList() {
  tft.fillScreen(TFT_BLACK);

  String title;
  switch (slMode) {
    case SL_MODE_CHOOSER:    title = strChooseSourceTitle; break;
    case SL_MODE_USB_BROWSE: title = String("< ") + slCurrentFolderName; break;
    default:                 title = slCameFromChooser ? (String("< ") + strStationListTitle) : String(strStationListTitle); break;
  }
  drawTruncated(title, TEXT_X, 10, SCREEN_W - 60, 2, TFT_BLUE, TL_DATUM);

  // Close "X", top-right - hand-drawn crossing lines rather than another
  // icon asset, same spirit as drawWiFiQuality()'s hand-drawn bars.
  int cx0 = SCREEN_W - 26, cy0 = 10, cs = 14;
  tft.drawLine(cx0, cy0, cx0 + cs, cy0 + cs, TFT_WHITE);
  tft.drawLine(cx0, cy0 + cs, cx0 + cs, cy0, TFT_WHITE);

  // Thick blue rule under the title so it reads as a section heading -
  // doubles as the header/list divider (replaces a plain 1px hairline).
  tft.fillRect(0, SL_HEADER_H - 3, SCREEN_W, 3, TFT_BLUE);
  tft.drawFastHLine(0, SL_FOOTER_Y, SCREEN_W, TFT_DARKGREY);

  int rowCount = slRowCount();
  int visible = rowCount - stationScrollOffset;
  if (visible > SL_VISIBLE_ROWS) visible = SL_VISIBLE_ROWS;

  bool showPlayIcons = (slMode != SL_MODE_CHOOSER);  // the chooser's 2 rows are navigation, not playable content

  for (int i = 0; i < SL_VISIBLE_ROWS; i++) {
    int rowY = SL_LIST_Y0 + i * SL_ROW_H;
    if (i < visible) {
      int idx = stationScrollOffset + i;
      int textX = showPlayIcons ? SL_ROW_TEXT_X : TEXT_X;
      int textMaxW = showPlayIcons ? SL_ROW_TEXT_MAXW : (SCREEN_W - 2 * TEXT_X);
      drawTruncated(slRowName(idx), textX, rowY + 9, textMaxW, 2, TFT_WHITE, TL_DATUM);
      if (showPlayIcons) {
        int iconY = rowY + (SL_ROW_H - SL_PLAY_ICON_SIZE) / 2;
        artworkDrawIcon(stationPlayIcon_IMAGE, SL_PLAY_ICON_X, iconY, SL_PLAY_ICON_SRC, SL_PLAY_ICON_SIZE);
      }
    }
    tft.drawFastHLine(0, rowY + SL_ROW_H - 1, SCREEN_W, TFT_DARKGREY);
  }

  bool hasPrev = stationScrollOffset > 0;
  bool hasNext = stationScrollOffset + SL_VISIBLE_ROWS < rowCount;
  tft.loadFont(AA_FONT_SMALL, LittleFS);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(hasPrev ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
  tft.drawString("< Prev", SCREEN_W / 4, SL_FOOTER_Y + SL_FOOTER_H / 2, 2);
  tft.setTextColor(hasNext ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Next >", 3 * SCREEN_W / 4, SL_FOOTER_Y + SL_FOOTER_H / 2, 2);
  tft.unloadFont();
}

// Header tap that isn't the close "X" - goes back one level, if this mode
// is reachable from somewhere (the chooser itself, and favorites reached by
// auto-skipping the chooser because no USB is mounted, are both true top
// levels - nothing to go back to, so this is a no-op there).
static void slGoBack() {
  switch (slMode) {
    case SL_MODE_USB_BROWSE:
      usbBrowseGoBack();
      break;
    case SL_MODE_FAVORITES:
      if (slCameFromChooser) {
        slMode = SL_MODE_CHOOSER;
        stationScrollOffset = 0;
        drawStationList();
      }
      break;
    case SL_MODE_CHOOSER:
      break;  // already at the top
  }
}

// A row was tapped - what happens depends entirely on which screen we're
// showing.
static void slRowTapped(int idx) {
  switch (slMode) {
    case SL_MODE_CHOOSER:
      if (idx == 0) {
        slCameFromChooser = true;
        openFavoritesList();
      } else if (idx == 1) {
        openUsbFolderList();
      }
      break;

    case SL_MODE_FAVORITES:
      if (idx < stationCount) {
        WebRadioStation chosen = stationList[idx];  // copy - stationList[] is about to go stale once the overlay closes
        closeStationList();
        volumioPlayWebRadio(chosen);
      }
      break;

    case SL_MODE_USB_BROWSE:
      if (idx >= stationCount) break;
      if (stationList[idx].isFolder) {
        if (usbStackDepth + 1 >= USB_STACK_MAX) break;  // absurdly deep nesting - ignore rather than overflow the stack
        usbStackDepth++;
        usbStackUri[usbStackDepth] = stationList[idx].uri;
        usbStackName[usbStackDepth] = stationList[idx].name;
        browseUsbCurrentLevel();
      } else {
        playUsbSongAt(idx);
      }
      break;
  }
}

// A row's Play icon was tapped - always plays immediately, no navigating.
// Identical to slRowTapped() for songs/favorites (both are already "play
// this" targets); the one real difference is USB folders, which
// slRowTapped() opens but this plays everything inside recursively instead
// (mirrors Volumio's own web UI - see volumioPlayUsbFolder()'s comment).
static void slRowPlayTapped(int idx) {
  switch (slMode) {
    case SL_MODE_FAVORITES:
      if (idx < stationCount) {
        WebRadioStation chosen = stationList[idx];
        closeStationList();
        volumioPlayWebRadio(chosen);
      }
      break;

    case SL_MODE_USB_BROWSE:
      if (idx >= stationCount) break;
      if (stationList[idx].isFolder) {
        WebRadioStation folder = stationList[idx];  // copy - stationList[] goes stale once the overlay closes
        closeStationList();
        volumioPlayUsbFolder(folder);
      } else {
        playUsbSongAt(idx);
      }
      break;

    case SL_MODE_CHOOSER:
      break;  // no play icon shown on these rows - see drawStationList()'s showPlayIcons
  }
}

// Routes all touch input while the overlay is up - close "X", header "< Back",
// Prev/Next paging, or a row (meaning depends on slMode - see slRowTapped()).
// Edge-triggered (slWasTouched) so holding a finger down doesn't repeat-fire,
// same reasoning as the main button row's pressedButtonIndex dedup, just
// tracked with a plain bool since rows aren't a fixed set.
static void stationListTouch(bool touched, uint16_t tx, uint16_t ty) {
  if (!touched) {
    slWasTouched = false;
    return;
  }
  if (slWasTouched) return;

  uint32_t now = millis();
  if (now - lastTouchMs < TOUCH_DEBOUNCE_MS) return;
  slWasTouched = true;
  lastTouchMs = now;

  if (ty < SL_HEADER_H) {
    if (tx >= SCREEN_W - 40) {
      closeStationList();
    } else {
      slGoBack();
    }
    return;
  }

  if (ty >= SL_FOOTER_Y) {
    int rowCount = slRowCount();
    if (tx < SCREEN_W / 2) {
      if (stationScrollOffset > 0) {
        stationScrollOffset -= SL_VISIBLE_ROWS;
        if (stationScrollOffset < 0) stationScrollOffset = 0;
        drawStationList();
      }
    } else if (stationScrollOffset + SL_VISIBLE_ROWS < rowCount) {
      stationScrollOffset += SL_VISIBLE_ROWS;
      drawStationList();
    }
    return;
  }

  if (ty >= SL_LIST_Y0 && ty < SL_FOOTER_Y) {
    int row = (ty - SL_LIST_Y0) / SL_ROW_H;
    int idx = stationScrollOffset + row;
    if (idx < slRowCount()) {
      // Play icon is the leading (leftmost) item on the row (chooser rows
      // don't have one - see drawStationList()'s showPlayIcons) - a
      // generous pad past its right edge so it's an easy target on a
      // resistive touchscreen.
      static const int PLAY_ICON_TOUCH_PAD_X = 10;
      if (slMode != SL_MODE_CHOOSER && tx <= SL_PLAY_ICON_X + SL_PLAY_ICON_SIZE + PLAY_ICON_TOUCH_PAD_X) {
        slRowPlayTapped(idx);
      } else {
        slRowTapped(idx);
      }
    }
  }
}

// ---------------------------------------------------------------------
// Touch screen calibration, runs from volumioremote.local
//
// The actual load/save/clear/calibrate logic lives in touch_cyd.cpp -
// this board's touch chip is on its own SPI bus, so it can't use
// TFT_eSPI's built-in calibrateTouch()/setTouch() the way the CrowPanel
// build does. displayInit() already calls touchCydLoadCalibration() /
// touchCydCalibrate() directly; requestTouchRecalibration() below is the
// only other call site (wired up from the settings web page).
// ---------------------------------------------------------------------

// Called from the settings web page - clears saved calibration and reboots
// back into touchCydCalibrate() (via displayInit(), on the next boot).
// Doesn't return.
void requestTouchRecalibration() {
  touchCydClearCalibration();
  displayShowStatus(strTouchCalCleared);
  delay(1000);
  ESP.restart();
}


// ---------------------------------------------------------------------
// Touch - hit-tests taps against the button rects and maps them to
// Volumio commands, debounced.
// ---------------------------------------------------------------------
static const int TOUCH_PAD_X = 3;
static const int TOUCH_PAD_Y = 10;

static bool inRect(int tx, int ty, ButtonRect r) {
  return tx >= r.x - TOUCH_PAD_X && tx <= r.x + r.w + TOUCH_PAD_X && ty >= r.y - TOUCH_PAD_Y && ty <= r.y + r.h + TOUCH_PAD_Y;
}

// Volume slider (webradio only) - bar is only PROGRESS_H (8px) tall, padded
// generously for a reliable hit.
static const int SLIDER_TOUCH_PAD_Y = 15;
static const uint32_t SLIDER_UPDATE_MS = 150;  // throttles volumioSetVolume() calls while dragging

static bool inSliderRect(int tx, int ty) {
  return tx >= TEXT_X && tx <= TEXT_X + TEXT_W && ty >= Y_PROGRESS_BAR - SLIDER_TOUCH_PAD_Y && ty <= Y_PROGRESS_BAR + PROGRESS_H + SLIDER_TOUCH_PAD_Y;
}

static bool sliderDragging = false;
static uint32_t lastSliderUpdateMs = 0;

static void touchPoll() {
  uint16_t tx, ty;
  bool touched = touchCydGetPoint(tx, ty);

  if (touched) {
    lastInteractionMs = millis();
    if (screensaverActive) {
      // Swallow the wake tap - don't also let it land on a button underneath.
      exitScreensaver();
      return;
    }

    // Throttled debug print - confirms taps land in sane screen coordinates.
    uint32_t nowLog = millis();
    if (nowLog - lastTouchLogMs >= TOUCH_LOG_INTERVAL_MS) {
      lastTouchLogMs = nowLog;
      Serial.printf("Touch at (%d, %d)\n", tx, ty);
    }
  }

  // Station-list overlay takes over touch entirely while it's up - it has
  // its own rows/paging/close-X, none of which map onto the main screen's
  // button rects below.
  if (stationListActive) {
    stationListTouch(touched, tx, ty);
    return;
  }

  // Volume slider takes over the touch entirely while active (webradio only).
  if (touched && currentState.service == "webradio" && inSliderRect(tx, ty)) {
    uint32_t now = millis();
    if (!sliderDragging || now - lastSliderUpdateMs >= SLIDER_UPDATE_MS) {
      lastSliderUpdateMs = now;
      int vol = (int)((long)(tx - TEXT_X) * 100 / TEXT_W);
      vol = constrain(vol, 0, 100);
      currentState.volume = vol;  // optimistic - next getState poll resyncs with the real value
      displayUpdateProgress(currentState);
      volumioSetVolume(vol);
    }
    sliderDragging = true;
    return;  // skip button hit-testing this poll
  }
  sliderDragging = false;

  int hit = -1;
  if (touched) {
    for (int i = 0; i < kButtonCount; i++) {
      // Transport group hidden/inert during AirPlay.
      if (!transportButtonsVisible && i < kTransportButtonCount) continue;
      if (inRect(tx, ty, kButtons[i].rect)) {
        hit = i;
        break;
      }
    }
  }

  if (hit == pressedButtonIndex) return;  // no change

  // Whatever was showing "pressed" goes back to normal - iconPathFor() is
  // evaluated fresh so a play/stop toggle mid-tap still shows correctly.
  if (pressedButtonIndex >= 0) {
    const IconButton &prev = kButtons[pressedButtonIndex];
    artworkDrawIcon(iconPathFor(pressedButtonIndex), prev.rect.x, prev.rect.y, BTN_ICON_SRC, prev.rect.w);
  }

  if (hit >= 0) {
    artworkDrawIconPressed(iconPathFor(hit), kButtons[hit].rect.x, kButtons[hit].rect.y, BTN_ICON_SRC, kButtons[hit].rect.w);

    // Fire on the press edge only (once per new landing), debounced so
    // contact-bounce flicker on the resistive panel can't double-fire.
    uint32_t now = millis();
    if (now - lastTouchMs >= TOUCH_DEBOUNCE_MS) {
      lastTouchMs = now;
      if (hit == kVolOffIndex) {
        volumeMuted = !volumeMuted;
        volumioCommand(volumeMuted ? "volume&volume=mute" : "volume&volume=unmute");
      } else if (hit == kStationListIndex) {
        openStationList();
      } else {
        if (hit == kPlayIndex) localPlaying = !localPlaying;  // optimistic - resynced from currentState.status every poll
        volumioCommand(kButtons[hit].command);
      }
    }
  }

  pressedButtonIndex = hit;
}

// ---------------------------------------------------------------------
// Screensaver - four selectable styles (Settings page > Screensaver style).
// Mode is resolved once, in enterScreensaver(); screensaverAnimate() just
// dispatches to whichever style is active on every loop() tick. To add a
// new style: give it the next SS_MODE_* number, write initX()/animateX(),
// forward-declare both below, and add a case to the two switches.
// ---------------------------------------------------------------------
static const uint8_t SS_MODE_BOUNCE = 0;  // DVD-logo style bouncing artist name
static const uint8_t SS_MODE_FIREWORKS = 1;
static const uint8_t SS_MODE_TETRIS = 2;
static const uint8_t SS_MODE_STARFIELD = 3;
static const uint8_t SS_MODE_MATRIX = 4;
static uint8_t ssActiveMode = SS_MODE_BOUNCE;

static const uint32_t SCREENSAVER_FRAME_MS = 60;  // ~25fps - shared by bounce/fireworks/starfield
static const int SCREENSAVER_SPEED_PX = 3;        // px moved per frame, each axis (bounce mode)

static const uint16_t kScreensaverColors[] = {
  TFT_CYAN, TFT_GREEN, TFT_YELLOW, TFT_MAGENTA, TFT_ORANGE, TFT_WHITE
};
static const int kScreensaverColorCount = sizeof(kScreensaverColors) / sizeof(kScreensaverColors[0]);

static uint32_t lastScreensaverFrameMs = 0;

// Declared here (not down in the Mode 0 section below) because
// exitScreensaver() references it and globals, unlike functions, must be
// declared before their first use in file order.
static TFT_eSprite ssSprite = TFT_eSprite(&tft);

static void initBounce();
static void animateBounce();
static void initFireworks();
static void animateFireworks();
static void initTetris();
static void animateTetris();
static void initStarfield();
static void animateStarfield();
static void initMatrix();
static void animateMatrix();

static void enterScreensaver() {
  screensaverActive = true;
  tft.fillScreen(TFT_BLACK);
  ssActiveMode = settingsGetScreensaverMode();
  lastScreensaverFrameMs = millis();

  switch (ssActiveMode) {
    case SS_MODE_FIREWORKS: initFireworks(); break;
    case SS_MODE_TETRIS:    initTetris();    break;
    case SS_MODE_STARFIELD: initStarfield(); break;
    case SS_MODE_MATRIX:    initMatrix();    break;
    default:                initBounce();    break;
  }

  Serial.println("Screensaver: entering (idle timeout reached), mode " + String(ssActiveMode));
}

static void screensaverAnimate() {
  switch (ssActiveMode) {
    case SS_MODE_FIREWORKS: animateFireworks(); break;
    case SS_MODE_TETRIS:    animateTetris();    break;
    case SS_MODE_STARFIELD: animateStarfield(); break;
    case SS_MODE_MATRIX:    animateMatrix();    break;
    default:                animateBounce();    break;
  }
}

static void redrawMainScreen() {
  tft.fillScreen(TFT_BLACK);

  lastTitle = "\x01";  // guaranteed not to equal any real title, even an empty one
  lastAlbumArtUrl = "";
  lastDrawnVolume = -1;
  lastProgressMode = -1;
  lastDrawnProgressFillPx = -1;
  volLabelDrawn = false;
  lastWifiQualityMs = millis();  // avoid an immediate redundant second draw right after the one below

  drawAllButtonIcons();
  setTransportButtonsVisible(!currentState.disableUiControls);
  drawWiFiQuality();
  displayShowTrack(currentState, true);
  displayUpdateProgress(currentState);

  lastInteractionMs = millis();
}

static void exitScreensaver() {
  screensaverActive = false;
  if (ssActiveMode == SS_MODE_BOUNCE) {
    ssSprite.unloadFont();
    ssSprite.deleteSprite();  // frees the buffer - not needed again until the next initBounce()
  }
  redrawMainScreen();
  Serial.println("Screensaver: exiting");
}

// --- Mode 0: Bounce - bouncing artist name, DVD-logo style -------------

static String ssText;  // captured once, when the screensaver starts - see initBounce()
static int ssX, ssY;   // current top-left of ssText's bounding box
static int ssDX, ssDY;
static int ssTextW, ssTextH;
static int ssColorIndex = 0;
static bool ssNeedsRecompose = true;

static void initBounce() {
  ssText = sanitizeForFont(currentState.artist.length() ? currentState.artist : String("VOLUMIO"));
  ssSprite.loadFont(AA_FONT_LARGE, LittleFS);
  ssTextW = ssSprite.textWidth(ssText, 4);

  int maxW = SCREEN_W - 20;  // trim artist name if need be
  if (ssTextW > maxW) {
    int ellipsisW = ssSprite.textWidth("...", 4);
    while (ssText.length() > 0 && ssSprite.textWidth(ssText, 4) + ellipsisW > maxW) {
      ssText.remove(ssText.length() - 1);
    }
    ssText += "...";
    ssTextW = ssSprite.textWidth(ssText, 4);
  }
  ssTextH = ssSprite.fontHeight(4);

  ssSprite.setColorDepth(16);
  ssSprite.createSprite(ssTextW, ssTextH);
  ssSprite.setTextDatum(TL_DATUM);

  ssX = random(0, max(1, SCREEN_W - ssTextW));
  ssY = random(0, max(1, SCREEN_H - ssTextH));
  ssDX = SCREENSAVER_SPEED_PX;
  ssDY = SCREENSAVER_SPEED_PX;
  ssColorIndex = 0;
  ssNeedsRecompose = true;  // force the first frame to actually draw into ssSprite
}

static void animateBounce() {
  uint32_t now = millis();
  if (now - lastScreensaverFrameMs < SCREENSAVER_FRAME_MS) return;
  lastScreensaverFrameMs = now;

  int oldX = ssX, oldY = ssY;
  if (ssDX > 0) tft.fillRect(oldX, oldY, ssDX, ssTextH, TFT_BLACK);
  else if (ssDX < 0) tft.fillRect(oldX + ssTextW + ssDX, oldY, -ssDX, ssTextH, TFT_BLACK);
  if (ssDY > 0) tft.fillRect(oldX, oldY, ssTextW, ssDY, TFT_BLACK);
  else if (ssDY < 0) tft.fillRect(oldX, oldY + ssTextH + ssDY, ssTextW, -ssDY, TFT_BLACK);

  ssX += ssDX;
  ssY += ssDY;

  bool bounced = false;
  if (ssX <= 0) {
    ssX = 0;
    ssDX = SCREENSAVER_SPEED_PX;
    bounced = true;
  } else if (ssX + ssTextW >= SCREEN_W) {
    ssX = SCREEN_W - ssTextW;
    ssDX = -SCREENSAVER_SPEED_PX;
    bounced = true;
  }
  if (ssY <= 0) {
    ssY = 0;
    ssDY = SCREENSAVER_SPEED_PX;
    bounced = true;
  } else if (ssY + ssTextH >= SCREEN_H) {
    ssY = SCREEN_H - ssTextH;
    ssDY = -SCREENSAVER_SPEED_PX;
    bounced = true;
  }

  if (bounced) {
    ssColorIndex = (ssColorIndex + 1) % kScreensaverColorCount;
    ssNeedsRecompose = true;
  }

  if (ssNeedsRecompose) {
    ssSprite.fillSprite(TFT_BLACK);
    ssSprite.setTextColor(kScreensaverColors[ssColorIndex], TFT_BLACK);
    ssSprite.drawString(ssText, 0, 0, 4);
    ssNeedsRecompose = false;
  }
  ssSprite.pushSprite(ssX, ssY);
}

// --- Mode 1: Fireworks - rockets launch, burst into particles, fade out ---

struct FwParticle {
  float x, y, vx, vy;
  int8_t life;
};
static const int FW_LIFE_MAX = 24;
static const int FW_PARTICLES = 14;
static const int FW_SLOTS = 5;       // concurrent fireworks in flight - bump for a busier sky
static const int FW_LAUNCH_PCT = 5;  // % chance per frame, per free slot, of a new launch

struct Firework {
  bool active;
  bool rising;
  float x, y, vy;
  int apexY;
  uint16_t color;
  FwParticle p[FW_PARTICLES];
};
static Firework fw[FW_SLOTS];

// Dims a 565 color toward black - pct is 0-255 (255 = full brightness).
static uint16_t scaleColor565(uint16_t c, uint8_t pct) {
  uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  r = (r * pct) >> 8;
  g = (g * pct) >> 8;
  b = (b * pct) >> 8;
  return (r << 11) | (g << 5) | b;
}

// Takes a slot index rather than a Firework& - Arduino auto-generates
// forward declarations for every function near the top of the file, before
// struct Firework is defined, and a struct-reference parameter there fails
// to parse (shows up as a baffling "declared void" error). Plain int/bool
// params sidestep that entirely.
static void fwLaunch(int slot) {
  Firework &f = fw[slot];
  f.active = true;
  f.rising = true;
  f.x = random(30, SCREEN_W - 30);
  f.y = SCREEN_H;
  f.vy = -6.0 - random(0, 20) / 10.0;
  f.apexY = random(SCREEN_H / 6, SCREEN_H / 2);  // higher/lower apex = taller/shorter show
  f.color = kScreensaverColors[random(0, kScreensaverColorCount)];
}

static void initFireworks() {
  for (int i = 0; i < FW_SLOTS; i++) fw[i].active = false;
}

static void animateFireworks() {
  uint32_t now = millis();
  if (now - lastScreensaverFrameMs < SCREENSAVER_FRAME_MS) return;
  lastScreensaverFrameMs = now;

  for (int i = 0; i < FW_SLOTS; i++) {
    Firework &f = fw[i];
    if (!f.active) {
      if (random(0, 100) < FW_LAUNCH_PCT) fwLaunch(i);
      continue;
    }

    if (f.rising) {
      tft.fillRect((int)f.x - 1, (int)f.y, 3, 4, TFT_BLACK);  // erase old trail dot
      f.y += f.vy;
      if (f.y <= f.apexY) {
        f.rising = false;
        for (int k = 0; k < FW_PARTICLES; k++) {
          float ang = (TWO_PI * k) / FW_PARTICLES + random(0, 100) / 1000.0;
          float speed = 1.5 + random(0, 20) / 10.0;
          f.p[k].x = f.x;
          f.p[k].y = f.y;
          f.p[k].vx = cos(ang) * speed;
          f.p[k].vy = sin(ang) * speed;
          f.p[k].life = FW_LIFE_MAX;
        }
      } else {
        tft.fillRect((int)f.x - 1, (int)f.y, 3, 4, f.color);
      }
      continue;
    }

    bool anyAlive = false;
    for (int k = 0; k < FW_PARTICLES; k++) {
      FwParticle &pt = f.p[k];
      if (pt.life <= 0) continue;
      tft.fillRect((int)pt.x, (int)pt.y, 2, 2, TFT_BLACK);  // erase old
      pt.x += pt.vx;
      pt.y += pt.vy;
      pt.vy += 0.12;  // gravity - raise for a snappier fall
      pt.life--;
      if (pt.life > 0 && pt.x >= 0 && pt.x < SCREEN_W && pt.y >= 0 && pt.y < SCREEN_H) {
        uint8_t pct = (uint8_t)(255 * pt.life / FW_LIFE_MAX);
        tft.fillRect((int)pt.x, (int)pt.y, 2, 2, scaleColor565(f.color, pct));
        anyAlive = true;
      } else {
        pt.life = 0;
      }
    }
    if (!anyAlive) f.active = false;  // slot freed, eligible to relaunch
  }
}

// --- Mode 2: Tetris - random tetrominoes stack up, flash, reset --------

static const int TETRIS_CELL = 20;
static const int TETRIS_COLS = SCREEN_W / TETRIS_CELL;  // 16
static const int TETRIS_ROWS = SCREEN_H / TETRIS_CELL;  // 24
static const uint32_t TETRIS_STEP_MS = 220;             // time between one-row drops - lower is faster

// Each shape is 4 cells as {col,row} offsets from its spawn anchor.
static const int8_t kTetrominoes[5][4][2] = {
  { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } },  // I
  { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } },  // O
  { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 1, 1 } },  // T
  { { 1, 0 }, { 2, 0 }, { 0, 1 }, { 1, 1 } },  // S
  { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 1, 2 } },  // L
};
static const int kTetrominoWidths[5] = { 4, 2, 3, 3, 2 };  // keeps spawn columns in bounds

static uint16_t tetrisGrid[TETRIS_ROWS][TETRIS_COLS];  // 0 = empty; TFT_BLACK is never used as a piece color
static int8_t tShapeIdx;
static int tCol, tRow;
static uint16_t tColor;
static uint32_t lastTetrisStepMs = 0;
static uint32_t tetrisFlashUntilMs = 0;  // non-zero while the "screen full" flash is showing

static void tetrisClearGrid() {
  for (int r = 0; r < TETRIS_ROWS; r++)
    for (int c = 0; c < TETRIS_COLS; c++) tetrisGrid[r][c] = 0;
}

static bool tetrisBlocked(int col, int row) {
  if (col < 0 || col >= TETRIS_COLS || row >= TETRIS_ROWS) return true;
  if (row < 0) return false;  // still above the visible grid - not a collision yet
  return tetrisGrid[row][col] != 0;
}

static bool tetrisFits(int col, int row) {
  for (int i = 0; i < 4; i++) {
    if (tetrisBlocked(col + kTetrominoes[tShapeIdx][i][0], row + kTetrominoes[tShapeIdx][i][1])) return false;
  }
  return true;
}

// Draws (or erases, with color=TFT_BLACK) the current piece at its current position.
static void tetrisPaint(uint16_t color) {
  for (int i = 0; i < 4; i++) {
    int c = tCol + kTetrominoes[tShapeIdx][i][0];
    int r = tRow + kTetrominoes[tShapeIdx][i][1];
    if (r >= 0) tft.fillRect(c * TETRIS_CELL, r * TETRIS_CELL, TETRIS_CELL - 1, TETRIS_CELL - 1, color);
  }
}

// Simulates a straight drop from the spawn row and returns the row the
// piece would settle at - used to steer spawning toward gaps instead of
// picking a purely random column (see tetrisBestColumn() below).
static int tetrisDropRow(int col) {
  int r = -3;  // matches the real spawn row
  while (tetrisFits(col, r + 1)) r++;
  return r;
}


static int tetrisHolesForDrop(int col) {
  int r = tetrisDropRow(col);
  int width = kTetrominoWidths[tShapeIdx];
  int bottomRow[TETRIS_COLS];
  for (int w = 0; w < width; w++) bottomRow[w] = -1;
  for (int i = 0; i < 4; i++) {
    int dx = kTetrominoes[tShapeIdx][i][0];
    int dy = kTetrominoes[tShapeIdx][i][1];
    if (r + dy > bottomRow[dx]) bottomRow[dx] = r + dy;
  }

  int holes = 0;
  for (int w = 0; w < width; w++) {
    if (bottomRow[w] < 0) continue;  // shouldn't happen - our shapes are contiguous - but cheap to guard
    int c = col + w;
    for (int rr = bottomRow[w] + 1; rr < TETRIS_ROWS; rr++) {
      if (tetrisGrid[rr][c] != 0) break;
      holes++;
    }
  }
  return holes;
}

// Picks a start column that buries the fewest cells (ideally zero - a
// flush fit). Every column tied for the lowest hole count is pooled and
// chosen from at random, so it's not the exact same spot every time.
static int tetrisBestColumn() {
  int width = kTetrominoWidths[tShapeIdx];
  int bestHoles = 1000;
  for (int c = 0; c <= TETRIS_COLS - width; c++) {
    int h = tetrisHolesForDrop(c);
    if (h < bestHoles) bestHoles = h;
  }

  int candidates[TETRIS_COLS];
  int nCandidates = 0;
  for (int c = 0; c <= TETRIS_COLS - width; c++) {
    if (tetrisHolesForDrop(c) <= bestHoles) candidates[nCandidates++] = c;
  }
  return candidates[random(0, nCandidates)];
}

static void tetrisSpawn() {
  tShapeIdx = random(0, 5);
  tCol = tetrisBestColumn();
  tRow = -3;  // starts above the screen, falls into view
  tColor = kScreensaverColors[random(0, kScreensaverColorCount)];
}

// Repaints every cell from the grid array - used after a line clear shifts
// rows around, since that's cheaper to just redraw wholesale than to track
// exactly which cells moved where.
static void tetrisRedrawGrid() {
  for (int r = 0; r < TETRIS_ROWS; r++) {
    for (int c = 0; c < TETRIS_COLS; c++) {
      uint16_t color = tetrisGrid[r][c];
      tft.fillRect(c * TETRIS_CELL, r * TETRIS_CELL, TETRIS_CELL - 1, TETRIS_CELL - 1, color ? color : TFT_BLACK);
    }
  }
}

static int tetrisClearFullRows() {
  int cleared = 0;
  for (int r = TETRIS_ROWS - 1; r >= 0; r--) {
    bool full = true;
    for (int c = 0; c < TETRIS_COLS; c++) {
      if (tetrisGrid[r][c] == 0) {
        full = false;
        break;
      }
    }
    if (!full) continue;

    for (int c = 0; c < TETRIS_COLS; c++) {
      tft.fillRect(c * TETRIS_CELL, r * TETRIS_CELL, TETRIS_CELL - 1, TETRIS_CELL - 1, TFT_WHITE);
    }
    delay(80);  // brief flash - rare enough (one full row, not every frame) that blocking here is fine

    for (int rr = r; rr > 0; rr--) {
      for (int c = 0; c < TETRIS_COLS; c++) tetrisGrid[rr][c] = tetrisGrid[rr - 1][c];
    }
    for (int c = 0; c < TETRIS_COLS; c++) tetrisGrid[0][c] = 0;

    cleared++;
    r++;  // re-check this row - it now holds what used to be one row further up
  }
  return cleared;
}

static void initTetris() {
  tetrisClearGrid();
  tetrisFlashUntilMs = 0;
  tetrisSpawn();
  lastTetrisStepMs = millis();
}

static void animateTetris() {
  uint32_t now = millis();

  if (tetrisFlashUntilMs) {
    if (now < tetrisFlashUntilMs) return;
    tft.fillScreen(TFT_BLACK);
    tetrisClearGrid();
    tetrisSpawn();
    tetrisFlashUntilMs = 0;
    lastTetrisStepMs = now;
    return;
  }

  if (now - lastTetrisStepMs < TETRIS_STEP_MS) return;
  lastTetrisStepMs = now;

  tetrisPaint(TFT_BLACK);  // clear the piece from its current spot

  if (tetrisFits(tCol, tRow + 1)) {
    tRow++;
    tetrisPaint(tColor);
    return;
  }

  // Can't fall further - lock it into the grid at its resting spot.
  bool toppedOut = false;
  for (int i = 0; i < 4; i++) {
    int c = tCol + kTetrominoes[tShapeIdx][i][0];
    int r = tRow + kTetrominoes[tShapeIdx][i][1];
    if (r < 2) {
      toppedOut = true;  // locked at/above row 2 - call the screen full
      continue;
    }
    tetrisGrid[r][c] = tColor;
  }
  tetrisPaint(tColor);

  if (toppedOut) {
    // Safety valve only - with line clears below, columns essentially never
    // stack this high, but a stray one-in-a-thousand pileup still resets
    // cleanly instead of jamming.
    tft.fillScreen(TFT_WHITE);
    tetrisFlashUntilMs = now + 400;
    return;
  }

  if (tetrisClearFullRows() > 0) {
    tetrisRedrawGrid();  // rows shifted around - repaint from the grid array
  }
  tetrisSpawn();
}

// --- Mode 3: Starfield - points streak outward from center, warp-speed style ---

struct Star {
  float angle, dist, speed;
  uint16_t color;
};
static const int STARFIELD_COUNT = 60;
static const float STARFIELD_MAXDIST = 170.0;  // a bit past the screen's half-diagonal (240x320 vs. the CrowPanel's 320x480 - scaled down to match)
static Star stars[STARFIELD_COUNT];
static int lastStarX[STARFIELD_COUNT], lastStarY[STARFIELD_COUNT];
static bool lastStarValid[STARFIELD_COUNT];

// Index param, not Star& - see the comment on fwLaunch() above for why.
static void starReset(int idx, bool spreadOut) {
  Star &s = stars[idx];
  s.angle = random(0, 3600) / 10.0 * DEG_TO_RAD;
  s.dist = spreadOut ? random(0, (int)STARFIELD_MAXDIST) : 0;
  s.speed = 1.0 + random(0, 20) / 10.0;
  s.color = kScreensaverColors[random(0, kScreensaverColorCount)];
}

static void initStarfield() {
  for (int i = 0; i < STARFIELD_COUNT; i++) {
    starReset(i, true);  // spread initial distances so they don't all start at center together
    lastStarValid[i] = false;
  }
}

static void animateStarfield() {
  uint32_t now = millis();
  if (now - lastScreensaverFrameMs < SCREENSAVER_FRAME_MS) return;
  lastScreensaverFrameMs = now;

  int cx = SCREEN_W / 2, cy = SCREEN_H / 2;
  for (int i = 0; i < STARFIELD_COUNT; i++) {
    Star &s = stars[i];
    if (lastStarValid[i]) tft.fillRect(lastStarX[i], lastStarY[i], 2, 2, TFT_BLACK);

    s.dist += s.speed;
    s.speed += 0.03;  // accelerates outward for the "warp" feel

    int x = cx + (int)(cos(s.angle) * s.dist);
    int y = cy + (int)(sin(s.angle) * s.dist);
    if (s.dist > STARFIELD_MAXDIST || x < 0 || x >= SCREEN_W - 1 || y < 0 || y >= SCREEN_H - 1) {
      starReset(i, false);
      lastStarValid[i] = false;
      continue;
    }

    // Dim near the center, full color once further out - reuses the fade
    // helper from Mode 1 above; floor of 70 keeps close-in stars visible.
    uint8_t pct = (uint8_t)constrain((s.dist / STARFIELD_MAXDIST) * 255.0, 70, 255);
    tft.fillRect(x, y, 2, 2, scaleColor565(s.color, pct));
    lastStarX[i] = x;
    lastStarY[i] = y;
    lastStarValid[i] = true;
  }
}

// --- Mode 4: Matrix - green digital rain, one falling glyph per column ---

static const int MATRIX_CHAR_W = 12;
static const int MATRIX_CHAR_H = 16;
static const int MATRIX_COLS = SCREEN_W / MATRIX_CHAR_W;
static const int MATRIX_ROWS = SCREEN_H / MATRIX_CHAR_H;
static const char kMatrixChars[] = "01ABCDEFGHIJKLMNOPQRSTUVWXYZ$%#@&*+=?";
static const int kMatrixCharCount = sizeof(kMatrixChars) - 1;  // exclude the trailing null

struct RainColumn {
  int headRow;    // current head position, in character-cell rows (can be negative before it enters)
  int trailLen;   // how many rows behind the head stay lit before getting erased
  int stepMs;     // ms between one-row drops - varies per column for a less uniform look
};
static RainColumn rain[MATRIX_COLS];
static uint32_t lastRainStepMs[MATRIX_COLS];

static void matrixSeedColumn(int c) {
  rain[c].headRow = random(-MATRIX_ROWS, 0);  // starts above the screen, staggered
  rain[c].trailLen = 8 + random(0, 10);
  rain[c].stepMs = 40 + random(0, 60);
}

static void initMatrix() {
  uint32_t now = millis();
  for (int c = 0; c < MATRIX_COLS; c++) {
    matrixSeedColumn(c);
    lastRainStepMs[c] = now;
  }
}

static void animateMatrix() {
  uint32_t now = millis();

  for (int c = 0; c < MATRIX_COLS; c++) {
    if (now - lastRainStepMs[c] < (uint32_t)rain[c].stepMs) continue;
    lastRainStepMs[c] = now;

    int x = c * MATRIX_CHAR_W;

    // Demote the old head to a dim trail glyph before it advances.
    if (rain[c].headRow >= 0 && rain[c].headRow < MATRIX_ROWS) {
      char ch = kMatrixChars[random(0, kMatrixCharCount)];
      tft.drawChar(x, rain[c].headRow * MATRIX_CHAR_H, ch, TFT_DARKGREEN, TFT_BLACK, 2);
    }

    rain[c].headRow++;

    // Erase trailLen rows behind the new head - keeps each streak a fixed length.
    int eraseRow = rain[c].headRow - rain[c].trailLen;
    if (eraseRow >= 0 && eraseRow < MATRIX_ROWS) {
      tft.fillRect(x, eraseRow * MATRIX_CHAR_H, MATRIX_CHAR_W, MATRIX_CHAR_H, TFT_BLACK);
    }

    // Draw the new head bright.
    if (rain[c].headRow >= 0 && rain[c].headRow < MATRIX_ROWS) {
      char ch = kMatrixChars[random(0, kMatrixCharCount)];
      tft.drawChar(x, rain[c].headRow * MATRIX_CHAR_H, ch, TFT_WHITE, TFT_BLACK, 2);
    }

    // Once the whole streak (head + trail) is past the bottom, restart this column.
    if (rain[c].headRow - rain[c].trailLen > MATRIX_ROWS) {
      matrixSeedColumn(c);
    }
  }
}

// ---------------------------------------------------------------------
// WiFi - captive-portal provisioning (WiFiManager), no hardcoded credentials.
// ---------------------------------------------------------------------
static void connectWifi() {
  wm.setAPCallback(configModeCallback);
  wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_SEC);
  wm.setTitle("Volumio Remote Setup");
  wm.setClass("invert");                // WiFiManager's built-in dark theme instead of the default plain page
  wm.setShowInfoUpdate(false);          // hide the noisy device-info panel on the portal's landing page
  wm.setHostname(WIFI_PORTAL_AP_NAME);  // rename if you want a different mDNS/DHCP hostname than the AP name
  wm.setBreakAfterConfig(true);         // return from autoConnect() once the portal saves creds, don't loop internally

  displayShowStatus(strConnectingWifi);
  bool ok = wm.autoConnect(WIFI_PORTAL_AP_NAME);

  if (!ok) {
    displayShowStatus(strSetupTimedOut);
    delay(2000);
    ESP.restart();
    return;
  }

  // Portal can return before the station interface finishes associating.
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  displayShowStatus(String(strConnected) + WiFi.localIP().toString());
}

// Called from the settings web page - forgets the saved network and
// reboots back into the WiFiManager portal. Doesn't return.
void requestWifiReset() {
  wm.resetSettings();
  displayShowStatus(strWifiCleared);
  delay(1000);
  ESP.restart();
}

// ---------------------------------------------------------------------
// WiFi quality bar graph, top right
// ---------------------------------------------------------------------
void drawWiFiQuality() {
  const byte numBars = 5;
  const byte barWidth = 3;
  const byte barHeight = 20;
  const byte barSpace = 1;
  const uint16_t barXPosBase = SCREEN_W - 25;
  const byte barYPosBase = 20;
  const uint16_t barColor = TFT_YELLOW;
  const uint16_t barBackColor = TFT_DARKGREY;

  int8_t quality = getWifiQuality();

  for (int8_t i = 0; i < numBars; i++) {
    byte barSpacer = i * barSpace;
    byte tempBarHeight = (barHeight / numBars) * (i + 1);
    for (int8_t j = 0; j < tempBarHeight; j++) {
      for (byte ii = 0; ii < barWidth; ii++) {
        byte nextBarThreshold = (i + 1) * (100 / numBars);
        byte currentBarThreshold = i * (100 / numBars);
        byte currentBarIncrements = (barHeight / numBars) * (i + 1);
        float rangePerBar = (100 / numBars);
        float currentBarStrength;
        if ((quality > currentBarThreshold) && (quality < nextBarThreshold)) {
          currentBarStrength = ((quality - currentBarThreshold) / rangePerBar) * currentBarIncrements;
        } else if (quality >= nextBarThreshold) {
          currentBarStrength = currentBarIncrements;
        } else {
          currentBarStrength = 0;
        }
        if (j < currentBarStrength) {
          tft.drawPixel((barXPosBase + barSpacer + ii) + (barWidth * i), barYPosBase - j, barColor);
        } else {
          tft.drawPixel((barXPosBase + barSpacer + ii) + (barWidth * i), barYPosBase - j, barBackColor);
        }
      }
    }
  }
}

int8_t getWifiQuality() {
  int32_t dbm = WiFi.RSSI();
  if (dbm <= -100) return 0;
  else if (dbm >= -50) return 100;
  else return 2 * (dbm + 100);
}

// ---------------------------------------------------------------------
// WiFi config AP screen
// ---------------------------------------------------------------------
void configModeCallback(WiFiManager *wm) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.loadFont(AA_FONT_LARGE, LittleFS);
  tft.drawString(String(WIFI_PORTAL_AP_NAME), SCREEN_W / 2, SCREEN_H / 2, 4);
  tft.unloadFont();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.loadFont(AA_FONT_SMALL, LittleFS);
  tft.drawString(strApActive, SCREEN_W / 2, (SCREEN_H / 2) + 36, 2);
  tft.unloadFont();
  delay(2000);
}