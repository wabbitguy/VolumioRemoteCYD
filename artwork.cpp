#include "artwork.h"
#include "Settings.h"
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>

#if ART_LOG_VERBOSE  // Art: messages, set in Settings.h
  #define ART_LOG(...)   Serial.printf(__VA_ARGS__)
  #define ART_LOGLN(...) Serial.println(__VA_ARGS__)
#else
  #define ART_LOG(...)
  #define ART_LOGLN(...)
#endif

extern TFT_eSPI tft;

static void *artMalloc(size_t size) {  // PSRAM if there is one, else heap
  void *p = ps_malloc(size);
  return p ? p : malloc(size);
}

// ---- JPEG to screen, clipped to the art box ----
static int gArtX = 0, gArtY = 0, gArtSize = 0;

static bool jpegRenderCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  int boxRight  = gArtX + gArtSize;
  int boxBottom = gArtY + gArtSize;

  if (x >= boxRight || y >= boxBottom) return true;               // tile fully right/below the box
  if (x + (int)w <= gArtX || y + (int)h <= gArtY) return true;    // tile fully left/above the box

  int drawX = x, drawY = y;
  int drawW = w, drawH = h;
  int srcOffX = 0, srcOffY = 0;

  if (drawX < gArtX)              { srcOffX = gArtX - drawX; drawW -= srcOffX; drawX = gArtX; }
  if (drawY < gArtY)              { srcOffY = gArtY - drawY; drawH -= srcOffY; drawY = gArtY; }
  if (drawX + drawW > boxRight)   drawW = boxRight  - drawX;
  if (drawY + drawH > boxBottom)  drawH = boxBottom - drawY;
  if (drawW <= 0 || drawH <= 0) return true;

  if (srcOffX == 0 && srcOffY == 0 && drawW == (int)w && drawH == (int)h) {
    tft.pushImage(drawX, drawY, drawW, drawH, bitmap);
  } else {
    for (int row = 0; row < drawH; row++) {
      uint16_t *rowPtr = bitmap + (srcOffY + row) * w + srcOffX;
      tft.pushImage(drawX, drawY + row, drawW, 1, rowPtr);
    }
  }
  return true;
}

void artworkInit() {
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(jpegRenderCallback);
}

static uint8_t fitScale(uint16_t jpgW, uint16_t jpgH, int boxSize) {  // TJpg_Decoder scales 1/1, 1/2, 1/4, 1/8 only
  uint8_t scale = 1;
  while (scale < 8 && (jpgW / scale > boxSize || jpgH / scale > boxSize)) {
    scale *= 2;
  }
  return scale;
}

// ---- Decode and resample to the box ----
static uint16_t *gArtBuf = nullptr;
static int gArtBufW = 0, gArtBufH = 0;

static bool artBufRenderCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (!gArtBuf) return false;
  for (int row = 0; row < h; row++) {
    int dstY = y + row;
    if (dstY < 0 || dstY >= gArtBufH) continue;
    int dstX = x;
    int copyW = w;
    if (dstX < 0) { copyW += dstX; dstX = 0; }
    if (dstX + copyW > gArtBufW) copyW = gArtBufW - dstX;
    if (copyW <= 0) continue;
    memcpy(gArtBuf + dstY * gArtBufW + dstX, bitmap + row * w, copyW * sizeof(uint16_t));
  }
  return true;
}

// ---- Download and draw a JPEG ----
static bool fetchAndDraw(const String &url, int x, int y, int size) {
  if (url.length() == 0) return false;

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.useHTTP10(true);  // no chunked replies
  if (!http.begin(url)) {
    ART_LOGLN("Art: fetchAndDraw http.begin() failed");
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    ART_LOG("Art: fetchAndDraw HTTP %d\n", code);
    http.end();
    return false;
  }

  int len = http.getSize();
  if (len <= 0 || len > 300000) { // sanity cap
    ART_LOG("Art: fetchAndDraw bad content length %d\n", len);
    http.end();
    return false;
  }

  uint8_t *buf = (uint8_t *) artMalloc(len);
  if (!buf) {
    ART_LOG("Art: fetchAndDraw artMalloc(%d) failed\n", len);
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  int read = 0;
  while (http.connected() && read < len) {
    size_t avail = stream->available();
    if (avail) {
      int r = stream->readBytes(buf + read, min((int)avail, len - read));
      read += r;
    } else {
      delay(1);
    }
  }
  http.end();

  if (read != len) {
    ART_LOG("Art: fetchAndDraw short read (%d of %d bytes)\n", read, len);
    free(buf);
    return false;
  }

  if (len >= 4 && !(buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF)) {  // not a JPEG
    const char *fmt = "unknown format";
    if (buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G') fmt = "PNG";
    else if (buf[0] == 'G' && buf[1] == 'I' && buf[2] == 'F') fmt = "GIF";
    else if (len >= 12 && memcmp(buf + 8, "WEBP", 4) == 0) fmt = "WEBP";
    ART_LOG("Art: fetchAndDraw got %s, not a JPEG - TJpg_Decoder can't draw this\n", fmt);
    free(buf);
    return false;
  }

  uint16_t jpgW = 0, jpgH = 0;
  uint8_t scale = 1;
  if (TJpgDec.getJpgSize(&jpgW, &jpgH, buf, len) == JDR_OK && jpgW && jpgH) {
    scale = fitScale(jpgW, jpgH, size);
  }
  TJpgDec.setJpgScale(scale);

  int drawW = jpgW ? min((int)(jpgW / scale), size) : size;
  int drawH = jpgH ? min((int)(jpgH / scale), size) : size;

  tft.fillRect(x, y, size, size, TFT_BLACK);

  bool ok;
  if (drawW == size && drawH == size) {
    gArtX = x;
    gArtY = y;
    gArtSize = size;
    ok = (TJpgDec.drawJpg(x, y, buf, len) == JDR_OK);
    if (!ok) ART_LOGLN("Art: fetchAndDraw TJpgDec.drawJpg() failed (direct path)");
  } else {
    uint16_t *srcBuf = (uint16_t *) artMalloc((size_t)drawW * drawH * sizeof(uint16_t));
    ok = false;
    if (!srcBuf) {
      ART_LOG("Art: fetchAndDraw artMalloc(%dx%d src) failed\n", drawW, drawH);
    } else {
      gArtBuf = srcBuf;
      gArtBufW = drawW;
      gArtBufH = drawH;
      TJpgDec.setCallback(artBufRenderCallback);
      bool decoded = (TJpgDec.drawJpg(0, 0, buf, len) == JDR_OK);
      TJpgDec.setCallback(jpegRenderCallback);  // restore the direct-to-screen callback
      gArtBuf = nullptr;

      if (!decoded) {
        ART_LOGLN("Art: fetchAndDraw TJpgDec.drawJpg() failed (buffered path)");
      } else {
        uint16_t *dstBuf = (uint16_t *) artMalloc((size_t)size * size * sizeof(uint16_t));
        if (!dstBuf) {
          ART_LOG("Art: fetchAndDraw artMalloc(%dx%d dst) failed\n", size, size);
        } else {
          for (int dy = 0; dy < size; dy++) {
            int sy = dy * drawH / size;
            const uint16_t *srcRow = srcBuf + sy * drawW;
            uint16_t *dstRow = dstBuf + dy * size;
            for (int dx = 0; dx < size; dx++) {
              dstRow[dx] = srcRow[dx * drawW / size];
            }
          }
          tft.pushImage(x, y, size, size, dstBuf);
          free(dstBuf);
          ok = true;
        }
      }
      free(srcBuf);
    }
  }

  TJpgDec.setJpgScale(1); // reset so the next caller isn't affected
  free(buf);
  return ok;
}

// ---- No-artwork placeholder ----
static bool artworkRenderPlaceholder(int x, int y, int size, const char *path = NO_ART_IMAGE) {
  if (!LittleFS.exists(path)) {
    ART_LOG("Art: placeholder image %s not found in LittleFS\n", path);
    return false;
  }

  uint16_t jpgW = 0, jpgH = 0;
  uint8_t scale = 1;
  if (TJpgDec.getFsJpgSize(&jpgW, &jpgH, path, LittleFS) == JDR_OK && jpgW && jpgH) {
    scale = fitScale(jpgW, jpgH, size);
  }
  TJpgDec.setJpgScale(scale);

  int drawW = jpgW ? min((int)(jpgW / scale), size) : size;
  int drawH = jpgH ? min((int)(jpgH / scale), size) : size;

  tft.fillRect(x, y, size, size, TFT_BLACK);

  bool ok;
  if (drawW == size && drawH == size) {
    gArtX = x;
    gArtY = y;
    gArtSize = size;
    ok = (TJpgDec.drawFsJpg(x, y, path, LittleFS) == JDR_OK);
  } else {
    uint16_t *srcBuf = (uint16_t *) artMalloc((size_t)drawW * drawH * sizeof(uint16_t));
    ok = false;
    if (srcBuf) {
      gArtBuf = srcBuf;
      gArtBufW = drawW;
      gArtBufH = drawH;
      TJpgDec.setCallback(artBufRenderCallback);
      bool decoded = (TJpgDec.drawFsJpg(0, 0, path, LittleFS) == JDR_OK);
      TJpgDec.setCallback(jpegRenderCallback);
      gArtBuf = nullptr;

      if (decoded) {
        uint16_t *dstBuf = (uint16_t *) artMalloc((size_t)size * size * sizeof(uint16_t));
        if (dstBuf) {
          for (int dy = 0; dy < size; dy++) {
            int sy = dy * drawH / size;
            const uint16_t *srcRow = srcBuf + sy * drawW;
            uint16_t *dstRow = dstBuf + dy * size;
            for (int dx = 0; dx < size; dx++) {
              dstRow[dx] = srcRow[dx * drawW / size];
            }
          }
          tft.pushImage(x, y, size, size, dstBuf);
          free(dstBuf);
          ok = true;
        }
      }
      free(srcBuf);
    }
  }

  TJpgDec.setJpgScale(1);
  if (!ok) ART_LOGLN("Art: placeholder image failed to decode (bad JPEG?)");
  return ok;
}

// ---- Artist/title parsing ----
static String normalizeArtistName(const String &artist) {  // "Last, First" -> "First Last" for iTunes
  int comma = artist.indexOf(',');
  if (comma < 0) return artist;
  if (artist.indexOf(',', comma + 1) >= 0) return artist;  // more than one comma isn't this pattern

  String last = artist.substring(0, comma);
  String first = artist.substring(comma + 1);
  last.trim();
  first.trim();
  return first + " " + last;
}

static bool splitArtistTitle(const String &combined, String &artist, String &title) {  // webradio title -> artist + track
  int dash = combined.indexOf(" - ");
  if (dash >= 0) {
    artist = normalizeArtistName(combined.substring(0, dash));
    title  = combined.substring(dash + 3);
    return true;
  }

  int firstComma = combined.indexOf(',');
  int secondComma = firstComma >= 0 ? combined.indexOf(',', firstComma + 1) : -1;
  if (firstComma >= 0 && secondComma >= 0 && combined.indexOf(',', secondComma + 1) < 0) {
    String last = combined.substring(0, firstComma);
    String first = combined.substring(firstComma + 1, secondComma);
    last.trim();
    first.trim();
    artist = first + " " + last;
    title = combined.substring(secondComma + 1);
    title.trim();
    return true;
  }

  return false;
}

// ---- Cover art lookups ----
static String urlEncode(const String &s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c)) out += c;
    else if (c == ' ') out += '+';
    else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      out += buf;
    }
  }
  return out;
}

static String lookupItunesArt(const String &artist, const String &title) {  // iTunes Search API, no key
  String term = urlEncode(artist + " " + title);
  String url = String("https://") + ITUNES_SEARCH_HOST +
               "/search?term=" + term + "&entity=song&limit=1";

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.useHTTP10(true);  // no chunked replies
  if (!http.begin(url)) return "";
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return "";
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) return "";

  int count = doc["resultCount"] | 0;
  if (count < 1) return "";

  String art100 = doc["results"][0]["artworkUrl100"] | "";
  if (art100.length() == 0) return "";

  art100.replace("100x100bb", String(ITUNES_ART_SIZE) + "bb");
  return art100;
}

static String lookupDiscogsArt(const String &artist, const String &title) {  // needs a Discogs token
  String token = settingsGetDiscogsToken();
  if (token.length() == 0) {
    ART_LOGLN("Art: Discogs token not set, skipping");
    return "";
  }

  String url = String("https://") + DISCOGS_SEARCH_HOST +
               "/database/search?type=release" +
               "&artist=" + urlEncode(artist) +
               "&track=" + urlEncode(title) +
               "&token=" + token;

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setUserAgent("VolumioRemoteDisplay/1.0");  // Discogs needs a User-Agent
  http.useHTTP10(true);  // no chunked replies
  if (!http.begin(url)) {
    ART_LOGLN("Art: Discogs http.begin() failed (bad URL?)");
    return "";
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    ART_LOG("Art: Discogs HTTP %d - check token in settings\n", code);
    http.end();
    return "";
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    ART_LOG("Art: Discogs JSON parse failed - %s\n", err.c_str());
    return "";
  }

  JsonArray results = doc["results"].as<JsonArray>();
  ART_LOG("Art: Discogs returned %u result(s)\n", (unsigned)results.size());
  if (results.size() == 0) return "";

  return results[0]["cover_image"] | "";
}

// ---- Button icons ----
static uint16_t *gIconBuf = nullptr;
static int gIconBufSize = 0;

static bool iconRenderCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (!gIconBuf) return false;
  for (int row = 0; row < h; row++) {
    int dstY = y + row;
    if (dstY < 0 || dstY >= gIconBufSize) continue;
    int dstX = x;
    int copyW = w;
    if (dstX < 0) { copyW += dstX; dstX = 0; }
    if (dstX + copyW > gIconBufSize) copyW = gIconBufSize - dstX;
    if (copyW <= 0) continue;
    memcpy(gIconBuf + dstY * gIconBufSize + dstX, bitmap + row * w, copyW * sizeof(uint16_t));
  }
  return true;
}

bool artworkDrawIcon(const char *path, int x, int y, int srcSize, int dstSize) {
  if (!LittleFS.exists(path)) {
    Serial.printf("Art: icon %s not found in LittleFS\n", path);
    return false;
  }

  if (srcSize == dstSize) {
    gArtX = x;
    gArtY = y;
    gArtSize = dstSize;
    TJpgDec.setJpgScale(1);
    bool ok = (TJpgDec.drawFsJpg(x, y, path, LittleFS) == JDR_OK);
    if (!ok) Serial.printf("Art: icon %s failed to decode (direct path)\n", path);
    return ok;
  }

  uint16_t *srcBuf = (uint16_t *) artMalloc((size_t)srcSize * srcSize * sizeof(uint16_t));
  if (!srcBuf) {
    Serial.printf("Art: icon %s - artMalloc(%dx%d src) failed\n", path, srcSize, srcSize);
    return false;
  }

  gIconBuf = srcBuf;
  gIconBufSize = srcSize;
  TJpgDec.setCallback(iconRenderCallback);
  TJpgDec.setJpgScale(1);
  bool decoded = (TJpgDec.drawFsJpg(0, 0, path, LittleFS) == JDR_OK);
  TJpgDec.setCallback(jpegRenderCallback);  // restore the album-art callback
  gIconBuf = nullptr;

  if (!decoded) {
    Serial.printf("Art: icon %s failed to decode (buffered path)\n", path);
    free(srcBuf);
    return false;
  }

  uint16_t *dstBuf = (uint16_t *) artMalloc((size_t)dstSize * dstSize * sizeof(uint16_t));
  if (!dstBuf) {
    Serial.printf("Art: icon %s - artMalloc(%dx%d dst) failed\n", path, dstSize, dstSize);
    free(srcBuf);
    return false;
  }

  for (int dy = 0; dy < dstSize; dy++) {
    int sy = dy * srcSize / dstSize;
    const uint16_t *srcRow = srcBuf + sy * srcSize;
    uint16_t *dstRow = dstBuf + dy * dstSize;
    for (int dx = 0; dx < dstSize; dx++) {
      dstRow[dx] = srcRow[dx * srcSize / dstSize];
    }
  }
  free(srcBuf);

  tft.pushImage(x, y, dstSize, dstSize, dstBuf);
  free(dstBuf);
  return true;
}

bool artworkDrawIconPressed(const char *path, int x, int y, int srcSize, int dstSize) {
  uint16_t *srcBuf = (uint16_t *) artMalloc((size_t)srcSize * srcSize * sizeof(uint16_t));
  if (!srcBuf) return false;

  gIconBuf = srcBuf;
  gIconBufSize = srcSize;
  TJpgDec.setCallback(iconRenderCallback);
  TJpgDec.setJpgScale(1);
  bool decoded = (TJpgDec.drawFsJpg(0, 0, path, LittleFS) == JDR_OK);
  TJpgDec.setCallback(jpegRenderCallback);  // restore the album-art callback
  gIconBuf = nullptr;

  if (!decoded) {
    free(srcBuf);
    return false;
  }

  uint16_t *dstBuf = (uint16_t *) artMalloc((size_t)dstSize * dstSize * sizeof(uint16_t));
  if (!dstBuf) {
    free(srcBuf);
    return false;
  }

  for (int dy = 0; dy < dstSize; dy++) {
    int sy = dy * srcSize / dstSize;
    const uint16_t *srcRow = srcBuf + sy * srcSize;
    uint16_t *dstRow = dstBuf + dy * dstSize;
    for (int dx = 0; dx < dstSize; dx++) {
      dstRow[dx] = ~srcRow[dx * srcSize / dstSize];  // invert colours
    }
  }
  free(srcBuf);

  tft.pushImage(x, y, dstSize, dstSize, dstBuf);
  free(dstBuf);
  return true;
}

static bool isGenericVolumioArt(const String &url) {  // Volumio's own no-art icon
  int qsAt = url.indexOf('?');
  String noQuery = (qsAt >= 0) ? url.substring(0, qsAt) : url;
  return noQuery.endsWith("/albumart");
}

// ---- Art fallback chain ----
bool artworkRender(const VolumioState &state, int x, int y, int size) {
  bool isWebradio = (state.service == "webradio");
  bool isAirplay = (state.service == "airplay_emulation");
  bool isLibrary = !isWebradio && !isAirplay;

  if (isWebradio && state.status != "play") {
    if (state.albumArtUrl.length() && fetchAndDraw(state.albumArtUrl, x, y, size)) return true;
    ART_LOGLN("Art: webradio stopped, Volumio's default artwork unavailable, drawing webRadioPause placeholder");
    return artworkRenderPlaceholder(x, y, size, webRadioPause);
  }

  if (isLibrary && state.albumArtUrl.length() && !isGenericVolumioArt(state.albumArtUrl)) {
    if (fetchAndDraw(state.albumArtUrl, x, y, size)) return true;
  }

  String artist, title;
  if (isWebradio) {
    if (!splitArtistTitle(state.title, artist, title)) {
      artist = state.artist;
      title = state.title;
    }
  } else {
    artist = state.artist;
    title = state.title;
  }

  if (artist.length() && title.length()) {
    ART_LOG("Art: looking up \"%s\" - \"%s\"\n", artist.c_str(), title.c_str());
    String art;

    if (isAirplay) {
      if (settingsGetUseItunes()) {
        art = lookupItunesArt(artist, title);
        if (art.length()) {
          ART_LOG("Art: iTunes hit - %s\n", art.c_str());
          if (fetchAndDraw(art, x, y, size)) return true;
          ART_LOGLN("Art: iTunes image fetch/decode failed, falling through");
          art = "";
        } else {
          ART_LOGLN("Art: iTunes miss");
        }
      } else {
        ART_LOGLN("Art: iTunes fallback disabled in settings, skipping");
      }

      if (art.length() == 0) {
        art = lookupDiscogsArt(artist, title);
        if (art.length()) {
          ART_LOG("Art: Discogs hit - %s\n", art.c_str());
          if (fetchAndDraw(art, x, y, size)) return true;
          ART_LOGLN("Art: Discogs image fetch/decode failed, falling through");
        }
      }
    } else {
      art = lookupDiscogsArt(artist, title);
      if (art.length()) {
        ART_LOG("Art: Discogs hit - %s\n", art.c_str());
        if (fetchAndDraw(art, x, y, size)) return true;
        ART_LOGLN("Art: Discogs image fetch/decode failed, falling through");
      }

      if (settingsGetUseItunes()) {
        art = lookupItunesArt(artist, title);
        if (art.length()) {
          ART_LOG("Art: iTunes hit - %s\n", art.c_str());
          if (fetchAndDraw(art, x, y, size)) return true;
          ART_LOGLN("Art: iTunes image fetch/decode failed, falling through");
        } else {
          ART_LOGLN("Art: iTunes miss");
        }
      } else {
        ART_LOGLN("Art: iTunes fallback disabled in settings, skipping");
      }
    }
  }

  if (state.albumArtUrl.length() && !isGenericVolumioArt(state.albumArtUrl)) {
    ART_LOGLN("Art: falling back to Volumio's own albumart");
    if (fetchAndDraw(state.albumArtUrl, x, y, size)) return true;
  }

  ART_LOGLN("Art: nothing worked, drawing placeholder");
  return artworkRenderPlaceholder(x, y, size);
}
