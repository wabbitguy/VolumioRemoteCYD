#include "volumio_api.h"
#include "Settings.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ---- URL helpers ----
static String baseUrl() {
  return String("http://") + settingsGetVolumioHost();
}

static String resolveAlbumArt(const String &raw) {
  if (raw.length() == 0) return "";
  if (raw.startsWith("http://") || raw.startsWith("https://")) return raw;
  return baseUrl() + raw;
}

// ---- getState ----
bool volumioGetState(VolumioState &out) {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  String url = baseUrl() + "/api/v1/getState";
  if (!http.begin(url)) return false;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) return false;

  out.valid = true;
  out.status = doc["status"] | "";
  out.title = doc["title"] | "";
  out.artist = doc["artist"] | "";
  out.album = doc["album"] | "";
  out.service = doc["service"] | "";
  out.volume = doc["volume"] | -1;
  out.mute = doc["mute"] | false;
  out.seek = doc["seek"] | 0;
  out.duration = doc["duration"] | 0;
  out.albumArtUrl = resolveAlbumArt(doc["albumart"] | "");
  out.disableUiControls = doc["disableUiControls"] | false;

  return true;
}

// ---- Transport and volume ----
bool volumioCommand(const String &cmd) {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  String url = baseUrl() + "/api/v1/commands/?cmd=" + cmd;
  if (!http.begin(url)) return false;
  int code = http.GET();
  http.end();
  return code == HTTP_CODE_OK;
}

bool volumioSetVolume(int vol) {
  vol = constrain(vol, 0, 100);
  return volumioCommand("volume&volume=" + String(vol));
}

// ---- Web radio favourites ----
int volumioGetFavouriteWebRadios(WebRadioStation out[], int maxCount) {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  String url = baseUrl() + "/api/v1/browse?uri=radio/favourites";  // the webradio plugin's Favorite Radios
  if (!http.begin(url)) return 0;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return 0;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) return 0;

  JsonArray lists = doc["navigation"]["lists"].as<JsonArray>();
  int count = 0;
  for (JsonObject list : lists) {
    JsonArray items = list["items"].as<JsonArray>();
    for (JsonObject item : items) {
      if (count >= maxCount) return count;

      String uri = item["uri"] | "";
      if (!uri.startsWith("http://") && !uri.startsWith("https://")) continue;  // folders aren't http(s) URLs

      String name = item["title"] | "";
      if (name.length() == 0) name = uri;

      out[count].name = name;
      out[count].uri = uri;
      count++;
    }
  }
  return count;
}

bool volumioPlayWebRadio(const WebRadioStation &station) {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  String url = baseUrl() + "/api/v1/replaceAndPlay";
  if (!http.begin(url)) return false;
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  JsonObject item = doc["item"].to<JsonObject>();
  item["uri"] = station.uri;
  item["service"] = "webradio";
  item["title"] = station.name;
  item["type"] = "webradio";

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  http.end();
  return code == HTTP_CODE_OK;
}

// ---- USB music ----
static String urlEncode(const String &s) {  // keeps '/' so paths work
  static const char *hex = "0123456789ABCDEF";
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == ':') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

// ---- Streaming browse reader (stops at maxCount) ----
static int streamReadByte(Stream &s) {  // idle timeout, not total
  uint32_t start = millis();
  while (!s.available()) {
    if (millis() - start > HTTP_TIMEOUT_MS) return -1;
    delay(1);
  }
  return s.read();
}

static int streamPeekByte(Stream &s) {
  uint32_t start = millis();
  while (!s.available()) {
    if (millis() - start > HTTP_TIMEOUT_MS) return -1;
    delay(1);
  }
  return s.peek();
}

static void streamSkipWhitespace(Stream &s) {
  int c;
  while ((c = streamPeekByte(s)) != -1) {
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
    streamReadByte(s);
  }
}

static bool streamExpect(Stream &s, char expected) {
  streamSkipWhitespace(s);
  return streamReadByte(s) == expected;
}

static String streamParseStringBody(Stream &s) {  // after the opening quote; handles \uXXXX
  String out;
  int c;
  while ((c = streamReadByte(s)) != -1) {
    if (c == '"') return out;
    if (c == '\\') {
      int esc = streamReadByte(s);
      if (esc == -1) break;
      switch (esc) {
        case '"':  out += '"'; break;
        case '\\': out += '\\'; break;
        case '/':  out += '/'; break;
        case 'b':  out += '\b'; break;
        case 'f':  out += '\f'; break;
        case 'n':  out += '\n'; break;
        case 'r':  out += '\r'; break;
        case 't':  out += '\t'; break;
        case 'u': {
          uint16_t code = 0;
          for (int i = 0; i < 4; i++) {
            int h = streamReadByte(s);
            if (h == -1) return out;
            code <<= 4;
            if (h >= '0' && h <= '9') code |= (h - '0');
            else if (h >= 'a' && h <= 'f') code |= (h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') code |= (h - 'A' + 10);
          }
          if (code < 0x80) {
            out += (char)code;
          } else if (code < 0x800) {
            out += (char)(0xC0 | (code >> 6));
            out += (char)(0x80 | (code & 0x3F));
          } else {
            out += (char)(0xE0 | (code >> 12));
            out += (char)(0x80 | ((code >> 6) & 0x3F));
            out += (char)(0x80 | (code & 0x3F));
          }
          break;
        }
        default: out += (char)esc; break;  // unknown escape - keep the literal char
      }
    } else {
      out += (char)c;
    }
  }
  return out;  // truncated
}

static void streamSkipValue(Stream &s) {  // skip a value we don't need
  streamSkipWhitespace(s);
  int c = streamPeekByte(s);
  if (c == '"') {
    streamReadByte(s);
    streamParseStringBody(s);
  } else if (c == '{' || c == '[') {
    char open = (char)c, close = (open == '{') ? '}' : ']';
    streamReadByte(s);
    int depth = 1;
    bool inString = false;
    while (depth > 0) {
      int ch = streamReadByte(s);
      if (ch == -1) return;  // truncated - best effort, give up
      if (inString) {
        if (ch == '\\') streamReadByte(s);  // skip escaped char, whatever it is
        else if (ch == '"') inString = false;
      } else {
        if (ch == '"') inString = true;
        else if (ch == open) depth++;
        else if (ch == close) depth--;
      }
    }
  } else {
    while ((c = streamPeekByte(s)) != -1) {
      if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
      streamReadByte(s);
    }
  }
}

static String streamConsumeStringValue(Stream &s) {  // string, or skip it if it isn't one
  streamSkipWhitespace(s);
  if (streamPeekByte(s) != '"') {
    streamSkipValue(s);
    return "";
  }
  streamReadByte(s);
  return streamParseStringBody(s);
}

static bool streamObjectNext(Stream &s, bool &first, String &keyOut) {  // next key, false at '}'
  streamSkipWhitespace(s);
  int c = streamPeekByte(s);
  if (c == '}') { streamReadByte(s); return false; }
  if (!first) {
    if (c != ',') return false;  // malformed - bail rather than loop on it
    streamReadByte(s);
    streamSkipWhitespace(s);
  }
  first = false;
  if (streamReadByte(s) != '"') return false;
  keyOut = streamParseStringBody(s);
  return streamExpect(s, ':');
}

static bool streamArrayNext(Stream &s, bool &first) {  // next element, false at ']'
  streamSkipWhitespace(s);
  int c = streamPeekByte(s);
  if (c == ']') { streamReadByte(s); return false; }
  if (!first) {
    if (c != ',') return false;
    streamReadByte(s);
    streamSkipWhitespace(s);
  }
  first = false;
  return streamPeekByte(s) != -1;
}

static int streamParseBrowseItems(Stream &s, WebRadioStation out[], int maxCount, bool requireKnownType) {  // requireKnownType: folders and songs only
  streamSkipWhitespace(s);
  if (streamReadByte(s) != '{') return 0;  // top-level object

  bool first = true;
  String key;
  int count = 0;
  while (streamObjectNext(s, first, key)) {
    if (key != "navigation") { streamSkipValue(s); continue; }

    streamSkipWhitespace(s);
    if (streamReadByte(s) != '{') break;

    bool navFirst = true;
    String navKey;
    while (streamObjectNext(s, navFirst, navKey)) {
      if (navKey != "lists") { streamSkipValue(s); continue; }

      streamSkipWhitespace(s);
      if (streamReadByte(s) != '[') break;

      bool listsFirst = true;
      while (streamArrayNext(s, listsFirst)) {
        streamSkipWhitespace(s);
        if (streamReadByte(s) != '{') break;  // one "list" object

        bool listFirst = true;
        String listKey;
        while (streamObjectNext(s, listFirst, listKey)) {
          if (listKey != "items") { streamSkipValue(s); continue; }

          streamSkipWhitespace(s);
          if (streamReadByte(s) != '[') break;

          bool itemsFirst = true;
          while (streamArrayNext(s, itemsFirst)) {
            streamSkipWhitespace(s);
            if (streamReadByte(s) != '{') break;  // one item object

            String title, uri, type;
            bool itemFirst = true;
            String itemKey;
            while (streamObjectNext(s, itemFirst, itemKey)) {
              if (itemKey == "title")     title = streamConsumeStringValue(s);
              else if (itemKey == "uri")  uri = streamConsumeStringValue(s);
              else if (itemKey == "type") type = streamConsumeStringValue(s);
              else streamSkipValue(s);
            }

            bool isFolder = (type == "folder");
            bool keep = uri.length() > 0 && (!requireKnownType || isFolder || type == "song");
            if (keep) {
              out[count].name = title.length() ? title : uri;
              out[count].uri = uri;
              out[count].isFolder = isFolder;
              count++;
              if (count >= maxCount) return count;  // stop reading, not just keeping
            }
          }
        }
      }
    }
  }

  return count;
}

static int browseUriStreaming(const String &uri, WebRadioStation out[], int maxCount, bool requireKnownType) {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  String url = baseUrl() + "/api/v1/browse?uri=" + urlEncode(uri);
  if (!http.begin(url)) return 0;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return 0;
  }

  int count = streamParseBrowseItems(http.getStream(), out, maxCount, requireKnownType);
  http.end();  // fine after an early exit
  return count;
}

bool volumioGetUsbRootUri(String &uriOut) {
  WebRadioStation first[1];
  int n = browseUriStreaming("music-library/USB", first, 1, false);
  if (n <= 0) return false;
  uriOut = first[0].uri;
  return true;
}

int volumioBrowseUsb(const String &uri, WebRadioStation out[], int maxCount) {
  return browseUriStreaming(uri, out, maxCount, true);
}

bool volumioPlayUsbSong(WebRadioStation songs[], int songCount, int startIndex) {
  if (startIndex < 0 || startIndex >= songCount) return false;

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  String url = baseUrl() + "/api/v1/replaceAndPlay";
  if (!http.begin(url)) return false;
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;  // list + index, so the folder plays on
  JsonArray list = doc["list"].to<JsonArray>();
  for (int i = 0; i < songCount; i++) {
    JsonObject it = list.add<JsonObject>();
    it["uri"] = songs[i].uri;
    it["service"] = "mpd";
    it["title"] = songs[i].name;
    it["type"] = "song";
  }
  doc["index"] = startIndex;
  JsonObject item = doc["item"].to<JsonObject>();
  item["uri"] = songs[startIndex].uri;
  item["service"] = "mpd";
  item["title"] = songs[startIndex].name;
  item["type"] = "song";

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  http.end();
  return code == HTTP_CODE_OK;
}

bool volumioPlayUsbFolder(const WebRadioStation &folder) {  // one-item list so playback stops at the folder's end
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  String url = baseUrl() + "/api/v1/replaceAndPlay";
  if (!http.begin(url)) return false;
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  JsonArray list = doc["list"].to<JsonArray>();
  JsonObject listItem = list.add<JsonObject>();
  listItem["uri"] = folder.uri;
  listItem["service"] = "mpd";
  listItem["title"] = folder.name;
  listItem["type"] = "folder";

  doc["index"] = 0;
  JsonObject item = doc["item"].to<JsonObject>();
  item["uri"] = folder.uri;
  item["service"] = "mpd";
  item["title"] = folder.name;
  item["type"] = "folder";

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  http.end();
  return code == HTTP_CODE_OK;
}

// ---- Playlists ----
static int streamParseNameArray(Stream &s, WebRadioStation out[], int maxCount) {  // listplaylists: flat array of names
  streamSkipWhitespace(s);
  if (streamReadByte(s) != '[') return 0;  // not a flat array

  int count = 0;
  bool first = true;
  while (streamArrayNext(s, first)) {
    String name = streamConsumeStringValue(s);
    if (name.length() == 0) continue;
    out[count].name = name;
    out[count].uri = "";  // unused
    count++;
    if (count >= maxCount) return count;  // stop reading
  }
  return count;
}

int volumioGetPlaylists(WebRadioStation out[], int maxCount) {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  String url = baseUrl() + "/api/v1/listplaylists";
  if (!http.begin(url)) return 0;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return 0;
  }

  int count = streamParseNameArray(http.getStream(), out, maxCount);
  http.end();  // fine after an early exit
  return count;
}

bool volumioPlayPlaylist(const String &name) {
  return volumioCommand("playplaylist&name=" + urlEncode(name));  // 200 even if the name doesn't exist
}

int volumioBrowsePlaylist(const String &name, WebRadioStation out[], int maxCount) {
  return browseUriStreaming("playlists/" + name, out, maxCount, true);
}

// ---- Settings page Test button ----
bool volumioTestHost(const String &host) {
  String trimmed = host;
  trimmed.trim();
  if (trimmed.length() == 0) return false;

  HTTPClient http;
  http.setTimeout(3000);  // short, it's a button click
  String url = String("http://") + trimmed + "/api/v1/getState";
  if (!http.begin(url)) return false;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) return false;

  String status = doc["status"] | "";  // only real Volumio replies have status
  return status.length() > 0;
}
