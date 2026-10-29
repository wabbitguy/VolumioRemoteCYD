/* CYD 2.8" - ESP32-2432S028R
   Driver ST7789 or ILI9341 (see the TFT_eSPI setup files)
   XPT2046 touch on its own separate SPI bus.

"ESP32 Dev Module"
Flash Size: 4MB (32Mb)
PSRAM: Disabled
Partition Scheme: "No OTA (2MB APP/2MB SPIFFS)"
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include "Settings.h"
#include "volumio_api.h"
#include "artwork.h"
#include "webui.h"
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
static const int TRACK_INDENT = 16;  // track line indent under artist

static const int PROGRESS_H = 8;

static const int VOL_NUMBER_X = SCREEN_W - TEXT_X - 40;
static const int VOL_NUMBER_CLEAR_W = 40;
static const int VOL_TEXT_CLEAR_H = 20;
static bool volLabelDrawn = false;

static const int SOURCE_MAXW = VOL_NUMBER_X - TEXT_X - 30;  // 30 = Vol label width + gap

static const int BTN_ICON_SRC = 34;   // native size of every button JPEG in data/
static const int BTN_ICON_SIZE = 34;  // bottom row icon size
static const int BTN_MARGIN = 3;      // left/right edge margin
static const int BTN_GAP = 4;         // gap within a group
static const int BTN_GROUP_GAP = 12;  // extra gap between the two groups
static const int BTN_Y = Y_BUTTONS_BOTTOM - BTN_ICON_SIZE;  // top of the icon row

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

static const int STATION_ICON_SIZE = 24;  // station-list button, top-left corner
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
static const int kVolOffIndex = 4;  // kButtons[] index of vol-off
static const int kPlayIndex = 1;  // kButtons[] index of play/stop
static const int kStationListIndex = 6;  // kButtons[] index of station list

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

static void displayInit();  // touch cal, clear screen, draw buttons
static void drawWrapped(const String &msg, int x, int y, int w, int font, uint16_t color);                          // multi-line word-wrapped text
static void drawTruncated(const String &msg, int x, int y, int maxWidth, int font, uint16_t color, uint8_t datum);  // one line, "..." if too long
static int measuredTextWidth(const String &msg, int font);  // width in px, no drawing
static int measuredFontHeight(int font);  // line height in px
static void displayShowStatus(const String &msg, bool fullClear = false);  // status message; fullClear wipes the whole screen
static void drawHeader(const VolumioState &state);                                                                  // artist line, top of screen
static void drawTextBlock(const VolumioState &state);                                                               // track/album/source lines below the art
static void displayShowTrack(const VolumioState &state, bool artChanged);  // redraw only what changed
static void displayUpdateProgress(const VolumioState &state);  // progress bar, or volume slider for webradio
void drawWiFiQuality();                                                                                             // signal-strength bars, top right
int8_t getWifiQuality();                                                                                            // RSSI dBm -> 0-100 signal quality

static void redrawMainScreen();
static void openStationList();
static void closeStationList();
static void drawStationList();
static void stationListTouch(bool touched, uint16_t tx, uint16_t ty);
static void openFavoritesList();  // webradio favorites
static void openUsbFolderList();                // enters USB browsing at the drive root
static void browseUsbCurrentLevel();  // fetch and show the current USB folder
static void usbBrowseGoBack();  // up one USB folder
static void playUsbSongAt(int idx);  // play song, queue the rest of the folder
static void openPlaylistsList();  // list saved playlists
static void openPlaylistSongs(const String &name);  // list songs in a playlist
static int slRowCount();                        // how many rows the current mode has
static String slRowName(int idx);               // row idx's display name in the current mode
static void slGoBack();  // header "< Back" tap
static void slRowTapped(int idx);  // row tap
static void slRowPlayTapped(int idx);  // row Play icon tap

static bool inRect(int tx, int ty, ButtonRect r);  // touch hit-test with padding
static void touchPoll();  // read touch, handle slider and buttons
static uint32_t lastTouchMs = 0;  // last button press time
static const uint32_t TOUCH_DEBOUNCE_MS = 300;  // min gap between button presses

static uint32_t lastTouchLogMs = 0;                 // millis() of last debug print
static const uint32_t TOUCH_LOG_INTERVAL_MS = 250;  // min gap between debug prints

static WiFiManager wm;      // captive-portal WiFi provisioning
static bool portalShown = false;  // portal blanked the screen
static void connectWifi();  // saved Wi-Fi, or the setup portal

static VolumioState currentState;  // last polled Volumio state
static uint32_t lastPollMs = 0;  // last getState poll time

static uint32_t lastWifiQualityMs = 0;                   // millis() of last signal-bars redraw
static const uint32_t WIFI_QUALITY_INTERVAL_MS = 60000;  // Wi-Fi bars redraw once a minute

static bool screensaverActive = false;
static uint32_t lastInteractionMs = 0;  // last touch time, for the screensaver
static void enterScreensaver();
static void exitScreensaver();
static void screensaverAnimate();

// ---- Station-list overlay state ----
static bool stationListActive = false;
static WebRadioStation stationList[MAX_STATIONS];
static int stationCount = 0;
static int stationScrollOffset = 0;  // first visible row
static bool slWasTouched = false;  // act on press only

static const int SL_HEADER_H = 34;
static const int SL_FOOTER_H = 34;
static const int SL_VISIBLE_ROWS = 7;
static const int SL_ROW_H = (SCREEN_H - SL_HEADER_H - SL_FOOTER_H) / SL_VISIBLE_ROWS;  // 36 - divides evenly at 240x320
static const int SL_LIST_Y0 = SL_HEADER_H;
static const int SL_FOOTER_Y = SCREEN_H - SL_FOOTER_H;

static const int SL_PLAY_ICON_SRC = 32;  // stationPlayIcon.jpg is 32x32
static const int SL_PLAY_ICON_SIZE = 24;  // on-screen size
static const int SL_PLAY_ICON_X = TEXT_X;                              // left-aligned - first item on the row
static const int SL_ROW_TEXT_X = SL_PLAY_ICON_X + SL_PLAY_ICON_SIZE + 8;  // title starts after the icon + a gap
static const int SL_ROW_TEXT_MAXW = SCREEN_W - SL_ROW_TEXT_X - TEXT_X;

enum StationListMode { SL_MODE_FAVORITES, SL_MODE_CHOOSER, SL_MODE_USB_BROWSE, SL_MODE_PLAYLISTS, SL_MODE_PLAYLIST_SONGS };
static StationListMode slMode = SL_MODE_FAVORITES;
static bool slCameFromChooser = false;  // shows a Back arrow on favorites
static String slCurrentFolderName;  // folder name for the Back label

static const int USB_STACK_MAX = 8;  // max USB folder depth
static String usbStackUri[USB_STACK_MAX];
static String usbStackName[USB_STACK_MAX];
static int usbStackDepth = 0;

enum ChooserSource { CH_FAVORITES, CH_USB, CH_PLAYLISTS };
static const int CHOOSER_SOURCE_MAX = 3;
static ChooserSource chooserSources[CHOOSER_SOURCE_MAX];
static int chooserSourceCount = 0;

// ---- Main ----
void setup() {
  Serial.begin(115200);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS init failed!");
    while (1) yield();
  }
  Serial.println("LittleFS ready.");

  settingsInit();  // load saved settings

  displayInit();// setup in portrait mode
  connectWifi(); // connect to wifi
  webuiInit();  // settings web page
  displayShowStatus(strWaitingForVolumio);

  drawWiFiQuality();
  lastWifiQualityMs = millis();
  lastInteractionMs = millis();  // screensaver idle timer starts now
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
        transportButtonsVisible = !currentState.disableUiControls;
        volumeMuted = currentState.mute;
        localPlaying = nowPlaying;
      } else {
        bool shouldShowTransport = !currentState.disableUiControls;  // AirPlay hides the transport buttons
        if (shouldShowTransport != transportButtonsVisible) {
          setTransportButtonsVisible(shouldShowTransport);
        }

        volumeMuted = currentState.mute;

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

// ---- Display: track, album, art ----
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
    touchCydCalibrate(tft, strTouchCalibrate, strCalFailed);
  }

  tft.fillScreen(TFT_BLACK);
  artworkInit();
  drawAllButtonIcons();
}

static void displayShowStatus(const String &msg, bool fullClear) {
  if (fullClear) {
    tft.fillScreen(TFT_BLACK);
    drawWrapped(msg, TEXT_X, ART_Y, SCREEN_W - 2 * TEXT_X, 2, TFT_WHITE);
    return;
  }
  int areaH = Y_ARTIST + TEXT_BLOCK_H;
  tft.fillRect(HEADER_CLEAR_X0, 0, HEADER_TEXT_MAXW, ART_Y, TFT_BLACK);
  tft.fillRect(0, ART_Y, SCREEN_W, areaH - ART_Y, TFT_BLACK);
  drawWrapped(msg, TEXT_X, ART_Y, SCREEN_W - 2 * TEXT_X, 2, TFT_WHITE);
}

static void drawHeader(const VolumioState &state) {
  drawTruncated(state.artist, SCREEN_W / 2, HEADER_Y, HEADER_TEXT_MAXW, 2, TFT_LIGHTGREY, TC_DATUM);
}

static String normalizeArtistName(const String &artist) {  // same as artwork.cpp's copy
  int comma = artist.indexOf(',');
  if (comma < 0) return artist;
  if (artist.indexOf(',', comma + 1) >= 0) return artist;  // more than one comma isn't this pattern

  String last = artist.substring(0, comma);
  String first = artist.substring(comma + 1);
  last.trim();
  first.trim();
  return first + " " + last;
}

static TitleParts splitTitle(const String &combined) {  // same as artwork.cpp's splitArtistTitle()
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

    parts.track = combined;  // no pattern, show as is
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

  bool hasArtistLine = parts.artist.length() > 0;

  String album = state.album.length() ? state.album : parts.album;
  bool hasAlbumLine = album.length() > 0;

  int artistFont = hasArtistLine ? ((measuredTextWidth(parts.artist, 4) <= TEXT_W) ? 4 : 2) : 2;
  int trackMaxW = TEXT_W - TRACK_INDENT;
  const int trackFont = 2;
  const int albumFont = 2;

  int hArtist = hasArtistLine ? measuredFontHeight(artistFont) : 0;
  int hSmall = measuredFontHeight(trackFont);
  int hTrack = hSmall;
  int hAlbum = hasAlbumLine ? hSmall : 0;

  int zoneTop = Y_ARTIST;
  int zoneH = TEXT_BLOCK_H;  // = Y_PROGRESS_BAR - Y_ARTIST (Settings.h)
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
    drawTruncated(album, TEXT_X, y - 4, TEXT_W, albumFont, TFT_LIGHTGREY, TL_DATUM);  // 4px up for a gap above the bar
  }

  tft.fillRect(TEXT_X, Y_VOL_NUMBER - 3, SOURCE_MAXW, VOL_TEXT_CLEAR_H, TFT_BLACK);
  drawTruncated(String(strSource) + state.service, TEXT_X, Y_VOL_NUMBER, SOURCE_MAXW, 2, TFT_DARKGREY, TL_DATUM);
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

static int lastProgressMode = -1;  // -1 = not drawn, 0 = webradio, 1 = other
static int lastDrawnVolume = -1;  // last drawn volume
static int lastDrawnProgressFillPx = -1;  // last drawn progress fill

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
    fillW = (barW - 2) * vol / 100;  // webradio: the bar is a volume slider
  } else if (state.duration > 0) {
    float frac = constrain((float)state.seek / 1000.0f / (float)state.duration, 0.0f, 1.0f);
    fillW = (int)(frac * (barW - 2));
  }

  bool modeChanged = (mode != lastProgressMode);
  bool volumeChanged = (vol != lastDrawnVolume);
  bool progressChanged = isWebradio ? false : (fillW != lastDrawnProgressFillPx);

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
    if (b0 == 0xE2 && i + 2 < len && (uint8_t)in[i + 1] == 0x80) {  // curly quotes, dashes, ... -> ASCII
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
        default: break;  // em dash is in the font
      }
    }
    out += (char)b0;
    i++;
  }
  return out;
}

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

// ---- Station-list overlay ----
static void openStationList() {
  pressedButtonIndex = -1;
  tft.fillScreen(TFT_BLACK);  // blank while checking sources, no Loading message

  String usbRootUri;
  bool hasUsb = volumioGetUsbRootUri(usbRootUri);

  stationCount = volumioGetPlaylists(stationList, MAX_STATIONS);  // presence check only
  bool hasPlaylists = stationCount > 0;

  chooserSourceCount = 0;
  chooserSources[chooserSourceCount++] = CH_FAVORITES;  // always offered
  if (hasUsb) chooserSources[chooserSourceCount++] = CH_USB;
  if (hasPlaylists) chooserSources[chooserSourceCount++] = CH_PLAYLISTS;

  if (chooserSourceCount <= 1) {
    slCameFromChooser = false;
    openFavoritesList();
    return;
  }

  slMode = SL_MODE_CHOOSER;
  stationScrollOffset = 0;
  slWasTouched = true;  // ignore the tap that opened this
  stationListActive = true;
  drawStationList();
}

static void closeStationList() {
  stationListActive = false;
  pressedButtonIndex = -1;
  redrawMainScreen();
}

static void openFavoritesList() {
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

static void openUsbFolderList() {
  usbStackDepth = 0;
  String rootUri;
  if (!volumioGetUsbRootUri(rootUri)) {
    slMode = SL_MODE_CHOOSER;
    stationScrollOffset = 0;
    drawStationList();
    return;
  }
  usbStackUri[0] = rootUri;
  usbStackName[0] = strUsbMusicTitle;
  browseUsbCurrentLevel();
}

static void browseUsbCurrentLevel() {
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

static void openPlaylistsList() {
  stationCount = volumioGetPlaylists(stationList, MAX_STATIONS);

  if (stationCount <= 0) {
    slMode = SL_MODE_CHOOSER;
    stationScrollOffset = 0;
    drawStationList();
    return;
  }

  slMode = SL_MODE_PLAYLISTS;
  stationScrollOffset = 0;
  slWasTouched = true;
  stationListActive = true;
  drawStationList();
}

static void openPlaylistSongs(const String &name) {
  stationCount = volumioBrowsePlaylist(name, stationList, MAX_STATIONS);
  slCurrentFolderName = name;

  if (stationCount <= 0) {
    displayShowStatus(strPlaylistEmpty, true);
    delay(1200);
    openPlaylistsList();
    return;
  }

  slMode = SL_MODE_PLAYLIST_SONGS;
  stationScrollOffset = 0;
  slWasTouched = true;
  stationListActive = true;
  drawStationList();
}

static void playUsbSongAt(int idx) {
  static WebRadioStation songsOnly[MAX_STATIONS];
  int songCount = 0;
  int startIndex = -1;
  for (int i = 0; i < stationCount; i++) {
    if (stationList[i].isFolder) continue;
    if (i == idx) startIndex = songCount;
    songsOnly[songCount++] = stationList[i];
  }
  if (startIndex < 0) return;

  closeStationList();
  volumioPlayUsbSong(songsOnly, songCount, startIndex);
}

static int slRowCount() {
  switch (slMode) {
    case SL_MODE_CHOOSER: return chooserSourceCount;
    default:              return stationCount;
  }
}

static String slRowName(int idx) {
  switch (slMode) {
    case SL_MODE_CHOOSER:
      switch (chooserSources[idx]) {
        case CH_FAVORITES: return String(strChoiceWebRadio);
        case CH_USB:       return String(strChoiceUsbMusic);
        case CH_PLAYLISTS: return String(strChoicePlaylists);
      }
      return "";
    default:
      if (slMode == SL_MODE_USB_BROWSE && stationList[idx].isFolder) return stationList[idx].name + " /";
      return stationList[idx].name;
  }
}

static void drawStationList() {
  tft.fillScreen(TFT_BLACK);

  String title;
  switch (slMode) {
    case SL_MODE_CHOOSER:        title = strChooseSourceTitle; break;
    case SL_MODE_USB_BROWSE:     title = String("< ") + slCurrentFolderName; break;
    case SL_MODE_PLAYLISTS:      title = String("< ") + strPlaylistsTitle; break;
    case SL_MODE_PLAYLIST_SONGS: title = String("< ") + slCurrentFolderName; break;  // playlist name
    default:                     title = slCameFromChooser ? (String("< ") + strStationListTitle) : String(strStationListTitle); break;
  }
  drawTruncated(title, TEXT_X, 10, SCREEN_W - 60, 2, TFT_GREEN, TL_DATUM);

  int cx0 = SCREEN_W - 26, cy0 = 10, cs = 14;  // close X, top-right
  tft.drawLine(cx0, cy0, cx0 + cs, cy0 + cs, TFT_WHITE);
  tft.drawLine(cx0, cy0 + cs, cx0 + cs, cy0, TFT_WHITE);

  tft.fillRect(0, SL_HEADER_H - 3, SCREEN_W, 3, TFT_GREEN);
  tft.drawFastHLine(0, SL_FOOTER_Y, SCREEN_W, TFT_DARKGREY);

  int rowCount = slRowCount();
  int visible = rowCount - stationScrollOffset;
  if (visible > SL_VISIBLE_ROWS) visible = SL_VISIBLE_ROWS;

  bool showPlayIcons = (slMode != SL_MODE_CHOOSER);  // no Play icons on the chooser

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
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  if (hasPrev) tft.drawString(strPrev, SCREEN_W / 4, SL_FOOTER_Y + SL_FOOTER_H / 2, 2);  // only shown if there's a page to go to
  if (hasNext) tft.drawString(strNext, 3 * SCREEN_W / 4, SL_FOOTER_Y + SL_FOOTER_H / 2, 2);
  tft.unloadFont();
}

static void slGoBack() {
  switch (slMode) {
    case SL_MODE_USB_BROWSE:
      usbBrowseGoBack();
      break;
    case SL_MODE_PLAYLIST_SONGS:
      openPlaylistsList();
      break;
    case SL_MODE_FAVORITES:
    case SL_MODE_PLAYLISTS:
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

static void slRowTapped(int idx) {
  switch (slMode) {
    case SL_MODE_CHOOSER:
      if (idx >= chooserSourceCount) break;
      switch (chooserSources[idx]) {
        case CH_FAVORITES:
          slCameFromChooser = true;
          openFavoritesList();
          break;
        case CH_USB:
          openUsbFolderList();
          break;
        case CH_PLAYLISTS:
          slCameFromChooser = true;
          openPlaylistsList();
          break;
      }
      break;

    case SL_MODE_FAVORITES:
      if (idx < stationCount) {
        WebRadioStation chosen = stationList[idx];  // copy, the list is about to be reused
        closeStationList();
        volumioPlayWebRadio(chosen);
      }
      break;

    case SL_MODE_PLAYLISTS:
      if (idx < stationCount) {
        String name = stationList[idx].name;
        openPlaylistSongs(name);
      }
      break;

    case SL_MODE_PLAYLIST_SONGS:
      if (idx < stationCount) {
        int startIndex = idx;
        int n = stationCount;
        closeStationList();
        volumioPlayUsbSong(stationList, n, startIndex);  // replaceAndPlay with list + index
      }
      break;

    case SL_MODE_USB_BROWSE:
      if (idx >= stationCount) break;
      if (stationList[idx].isFolder) {
        if (usbStackDepth + 1 >= USB_STACK_MAX) break;  // too deep, ignore
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

static void slRowPlayTapped(int idx) {
  switch (slMode) {
    case SL_MODE_FAVORITES:
      if (idx < stationCount) {
        WebRadioStation chosen = stationList[idx];
        closeStationList();
        volumioPlayWebRadio(chosen);
      }
      break;

    case SL_MODE_PLAYLISTS:
      if (idx < stationCount) {
        String name = stationList[idx].name;
        closeStationList();
        volumioPlayPlaylist(name);
      }
      break;

    case SL_MODE_PLAYLIST_SONGS:
      if (idx < stationCount) {
        int startIndex = idx;
        int n = stationCount;
        closeStationList();
        volumioPlayUsbSong(stationList, n, startIndex);
      }
      break;

    case SL_MODE_USB_BROWSE:
      if (idx >= stationCount) break;
      if (stationList[idx].isFolder) {
        WebRadioStation folder = stationList[idx];  // copy, the list is about to be reused
        closeStationList();
        volumioPlayUsbFolder(folder);
      } else {
        playUsbSongAt(idx);
      }
      break;

    case SL_MODE_CHOOSER:
      break;
  }
}

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
      static const int PLAY_ICON_TOUCH_PAD_X = 10;  // extra touch room right of the Play icon
      if (slMode != SL_MODE_CHOOSER && tx <= SL_PLAY_ICON_X + SL_PLAY_ICON_SIZE + PLAY_ICON_TOUCH_PAD_X) {
        slRowPlayTapped(idx);
      } else {
        slRowTapped(idx);
      }
    }
  }
}

// ---- Touch calibration ----
void requestTouchRecalibration() {  // called from the web page; reboots
  touchCydClearCalibration();
  displayShowStatus(strTouchCalCleared);
  delay(1000);
  ESP.restart();
}

// ---- Touch: buttons and slider ----
static const int TOUCH_PAD_X = 3;
static const int TOUCH_PAD_Y = 10;

static bool inRect(int tx, int ty, ButtonRect r) {
  return tx >= r.x - TOUCH_PAD_X && tx <= r.x + r.w + TOUCH_PAD_X && ty >= r.y - TOUCH_PAD_Y && ty <= r.y + r.h + TOUCH_PAD_Y;
}

static const int SLIDER_TOUCH_PAD_Y = 15;
static const uint32_t SLIDER_UPDATE_MS = 150;  // limits volume calls while dragging

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
      exitScreensaver();  // wake tap doesn't press a button
      return;
    }

    uint32_t nowLog = millis();
    if (nowLog - lastTouchLogMs >= TOUCH_LOG_INTERVAL_MS) {
      lastTouchLogMs = nowLog;
      Serial.printf("Touch at (%d, %d)\n", tx, ty);
    }
  }

  if (stationListActive) {
    stationListTouch(touched, tx, ty);
    return;
  }

  if (touched && currentState.service == "webradio" && inSliderRect(tx, ty)) {
    uint32_t now = millis();
    if (!sliderDragging || now - lastSliderUpdateMs >= SLIDER_UPDATE_MS) {
      lastSliderUpdateMs = now;
      int vol = (int)((long)(tx - TEXT_X) * 100 / TEXT_W);
      vol = constrain(vol, 0, 100);
      currentState.volume = vol;  // next poll gets the real value
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
      if (!transportButtonsVisible && i < kTransportButtonCount) continue;
      if (inRect(tx, ty, kButtons[i].rect)) {
        hit = i;
        break;
      }
    }
  }

  if (hit == pressedButtonIndex) return;  // no change

  if (pressedButtonIndex >= 0) {
    const IconButton &prev = kButtons[pressedButtonIndex];
    artworkDrawIcon(iconPathFor(pressedButtonIndex), prev.rect.x, prev.rect.y, BTN_ICON_SRC, prev.rect.w);
  }

  if (hit >= 0) {
    artworkDrawIconPressed(iconPathFor(hit), kButtons[hit].rect.x, kButtons[hit].rect.y, BTN_ICON_SRC, kButtons[hit].rect.w);

    uint32_t now = millis();
    if (now - lastTouchMs >= TOUCH_DEBOUNCE_MS) {
      lastTouchMs = now;
      if (hit == kVolOffIndex) {
        volumeMuted = !volumeMuted;
        volumioCommand(volumeMuted ? "volume&volume=mute" : "volume&volume=unmute");
      } else if (hit == kStationListIndex) {
        openStationList();
      } else {
        if (hit == kPlayIndex) localPlaying = !localPlaying;  // next poll gets the real state
        volumioCommand(kButtons[hit].command);
      }
    }
  }

  pressedButtonIndex = hit;
}

// ---- Screensaver ----
static const uint8_t SS_MODE_BOUNCE = 0;  // DVD-logo style bouncing artist name
static const uint8_t SS_MODE_FIREWORKS = 1;
static const uint8_t SS_MODE_TETRIS = 2;
static const uint8_t SS_MODE_STARFIELD = 3;
static const uint8_t SS_MODE_MATRIX = 4;
static uint8_t ssActiveMode = SS_MODE_BOUNCE;

static const uint32_t SCREENSAVER_FRAME_MS = 60;  // frame time for bounce/fireworks/starfield
static const int SCREENSAVER_SPEED_PX = 3;  // px per frame (bounce)

static const uint16_t kScreensaverColors[] = {
  TFT_CYAN, TFT_GREEN, TFT_YELLOW, TFT_MAGENTA, TFT_ORANGE, TFT_WHITE
};
static const int kScreensaverColorCount = sizeof(kScreensaverColors) / sizeof(kScreensaverColors[0]);

static uint32_t lastScreensaverFrameMs = 0;

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

  lastTitle = "\x01";  // can't match any real title
  lastAlbumArtUrl = "";
  lastDrawnVolume = -1;
  lastProgressMode = -1;
  lastDrawnProgressFillPx = -1;
  volLabelDrawn = false;
  lastWifiQualityMs = millis();  // skip a second redraw

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
    ssSprite.deleteSprite();  // free the sprite memory
  }
  redrawMainScreen();
  Serial.println("Screensaver: exiting");
}

// ---- Screensaver 0: Bounce ----
static String ssText;  // artist name captured at start
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
  ssNeedsRecompose = true;  // draw the first frame
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

// ---- Screensaver 1: Fireworks ----
struct FwParticle {
  float x, y, vx, vy;
  int8_t life;
};
static const int FW_LIFE_MAX = 24;
static const int FW_PARTICLES = 14;
static const int FW_SLOTS = 5;  // fireworks in the air at once
static const int FW_LAUNCH_PCT = 5;  // % chance of a launch per frame

struct Firework {
  bool active;
  bool rising;
  float x, y, vy;
  int apexY;
  uint16_t color;
  FwParticle p[FW_PARTICLES];
};
static Firework fw[FW_SLOTS];

static uint16_t scaleColor565(uint16_t c, uint8_t pct) {
  uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  r = (r * pct) >> 8;
  g = (g * pct) >> 8;
  b = (b * pct) >> 8;
  return (r << 11) | (g << 5) | b;
}

static void fwLaunch(int slot) {  // int slot, not Firework& (Arduino prototype bug)
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

// ---- Screensaver 2: Tetris ----
static const int TETRIS_CELL = 20;
static const int TETRIS_COLS = SCREEN_W / TETRIS_CELL;  // 16
static const int TETRIS_ROWS = SCREEN_H / TETRIS_CELL;  // 24
static const uint32_t TETRIS_STEP_MS = 220;  // ms per row drop

static const int8_t kTetrominoes[5][4][2] = {
  { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } },  // I
  { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } },  // O
  { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 1, 1 } },  // T
  { { 1, 0 }, { 2, 0 }, { 0, 1 }, { 1, 1 } },  // S
  { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 1, 2 } },  // L
};
static const int kTetrominoWidths[5] = { 4, 2, 3, 3, 2 };  // keeps spawn columns in bounds

static uint16_t tetrisGrid[TETRIS_ROWS][TETRIS_COLS];  // 0 = empty
static int8_t tShapeIdx;
static int tCol, tRow;
static uint16_t tColor;
static uint32_t lastTetrisStepMs = 0;
static uint32_t tetrisFlashUntilMs = 0;  // set while the full-screen flash shows

static void tetrisClearGrid() {
  for (int r = 0; r < TETRIS_ROWS; r++)
    for (int c = 0; c < TETRIS_COLS; c++) tetrisGrid[r][c] = 0;
}

static bool tetrisBlocked(int col, int row) {
  if (col < 0 || col >= TETRIS_COLS || row >= TETRIS_ROWS) return true;
  if (row < 0) return false;  // above the grid, no collision
  return tetrisGrid[row][col] != 0;
}

static bool tetrisFits(int col, int row) {
  for (int i = 0; i < 4; i++) {
    if (tetrisBlocked(col + kTetrominoes[tShapeIdx][i][0], row + kTetrominoes[tShapeIdx][i][1])) return false;
  }
  return true;
}

static void tetrisPaint(uint16_t color) {
  for (int i = 0; i < 4; i++) {
    int c = tCol + kTetrominoes[tShapeIdx][i][0];
    int r = tRow + kTetrominoes[tShapeIdx][i][1];
    if (r >= 0) tft.fillRect(c * TETRIS_CELL, r * TETRIS_CELL, TETRIS_CELL - 1, TETRIS_CELL - 1, color);
  }
}

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
    if (bottomRow[w] < 0) continue;
    int c = col + w;
    for (int rr = bottomRow[w] + 1; rr < TETRIS_ROWS; rr++) {
      if (tetrisGrid[rr][c] != 0) break;
      holes++;
    }
  }
  return holes;
}

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
    delay(80);  // brief flash

    for (int rr = r; rr > 0; rr--) {
      for (int c = 0; c < TETRIS_COLS; c++) tetrisGrid[rr][c] = tetrisGrid[rr - 1][c];
    }
    for (int c = 0; c < TETRIS_COLS; c++) tetrisGrid[0][c] = 0;

    cleared++;
    r++;  // row shifted down, check it again
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

  bool toppedOut = false;
  for (int i = 0; i < 4; i++) {
    int c = tCol + kTetrominoes[tShapeIdx][i][0];
    int r = tRow + kTetrominoes[tShapeIdx][i][1];
    if (r < 2) {
      toppedOut = true;  // locked near the top = screen full
      continue;
    }
    tetrisGrid[r][c] = tColor;
  }
  tetrisPaint(tColor);

  if (toppedOut) {
    tft.fillScreen(TFT_WHITE);  // screen full: flash and start over
    tetrisFlashUntilMs = now + 400;
    return;
  }

  if (tetrisClearFullRows() > 0) {
    tetrisRedrawGrid();  // rows moved, repaint
  }
  tetrisSpawn();
}

// ---- Screensaver 3: Starfield ----
struct Star {
  float angle, dist, speed;
  uint16_t color;
};
static const int STARFIELD_COUNT = 60;
static const float STARFIELD_MAXDIST = 170.0;  // just past the half-diagonal
static Star stars[STARFIELD_COUNT];
static int lastStarX[STARFIELD_COUNT], lastStarY[STARFIELD_COUNT];
static bool lastStarValid[STARFIELD_COUNT];

static void starReset(int idx, bool spreadOut) {  // int, not Star& (same as fwLaunch)
  Star &s = stars[idx];
  s.angle = random(0, 3600) / 10.0 * DEG_TO_RAD;
  s.dist = spreadOut ? random(0, (int)STARFIELD_MAXDIST) : 0;
  s.speed = 1.0 + random(0, 20) / 10.0;
  s.color = kScreensaverColors[random(0, kScreensaverColorCount)];
}

static void initStarfield() {
  for (int i = 0; i < STARFIELD_COUNT; i++) {
    starReset(i, true);  // spread out the first stars
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

    uint8_t pct = (uint8_t)constrain((s.dist / STARFIELD_MAXDIST) * 255.0, 70, 255);
    tft.fillRect(x, y, 2, 2, scaleColor565(s.color, pct));
    lastStarX[i] = x;
    lastStarY[i] = y;
    lastStarValid[i] = true;
  }
}

// ---- Screensaver 4: Matrix ----
static const int MATRIX_CHAR_W = 12;
static const int MATRIX_CHAR_H = 16;
static const int MATRIX_COLS = SCREEN_W / MATRIX_CHAR_W;
static const int MATRIX_ROWS = SCREEN_H / MATRIX_CHAR_H;
static const char kMatrixChars[] = "01ABCDEFGHIJKLMNOPQRSTUVWXYZ$%#@&*+=?";
static const int kMatrixCharCount = sizeof(kMatrixChars) - 1;  // exclude the trailing null

struct RainColumn {
  int headRow;  // head row (negative = not on screen yet)
  int trailLen;  // lit rows behind the head
  int stepMs;  // ms per row drop
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

    if (rain[c].headRow >= 0 && rain[c].headRow < MATRIX_ROWS) {
      char ch = kMatrixChars[random(0, kMatrixCharCount)];
      tft.drawChar(x, rain[c].headRow * MATRIX_CHAR_H, ch, TFT_DARKGREEN, TFT_BLACK, 2);
    }

    rain[c].headRow++;

    int eraseRow = rain[c].headRow - rain[c].trailLen;
    if (eraseRow >= 0 && eraseRow < MATRIX_ROWS) {
      tft.fillRect(x, eraseRow * MATRIX_CHAR_H, MATRIX_CHAR_W, MATRIX_CHAR_H, TFT_BLACK);
    }

    if (rain[c].headRow >= 0 && rain[c].headRow < MATRIX_ROWS) {
      char ch = kMatrixChars[random(0, kMatrixCharCount)];
      tft.drawChar(x, rain[c].headRow * MATRIX_CHAR_H, ch, TFT_WHITE, TFT_BLACK, 2);
    }

    if (rain[c].headRow - rain[c].trailLen > MATRIX_ROWS) {
      matrixSeedColumn(c);
    }
  }
}

// ---- WiFi ----
static void connectWifi() {
  wm.setAPCallback(configModeCallback);
  wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_SEC);
  wm.setTitle("Volumio Remote Setup");
  wm.setClass("invert");  // dark theme
  wm.setShowInfoUpdate(false);  // hide the device-info panel
  wm.setHostname(WIFI_PORTAL_AP_NAME);  // mDNS/DHCP hostname
  wm.setBreakAfterConfig(true);  // return once Wi-Fi is saved

  displayShowStatus(strConnectingWifi);
  bool ok = wm.autoConnect(WIFI_PORTAL_AP_NAME);

  if (!ok) {
    displayShowStatus(strSetupTimedOut);
    delay(2000);
    ESP.restart();
    return;
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  if (portalShown) {  // portal wiped the icons
    portalShown = false;
    redrawMainScreen();
  }
  displayShowStatus(String(strConnected) + WiFi.localIP().toString());
}

void requestWifiReset() {  // called from the web page; reboots
  wm.resetSettings();
  displayShowStatus(strWifiCleared);
  delay(1000);
  ESP.restart();
}

// ---- WiFi signal bars, top right ----
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

// ---- WiFi setup portal screen ----
void configModeCallback(WiFiManager *wm) {
  portalShown = true;
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