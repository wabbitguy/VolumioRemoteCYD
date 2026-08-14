#include "volumio_api.h"
#include "config.h"
#include "settings.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------
// URL helpers
// ---------------------------------------------------------------------
static String baseUrl() {
  return String("http://") + settingsGetVolumioHost();
}

static String resolveAlbumArt(const String &raw) {
  if (raw.length() == 0) return "";
  if (raw.startsWith("http://") || raw.startsWith("https://")) return raw;
  return baseUrl() + raw;
}

// ---------------------------------------------------------------------
// getState - polled playback state
// ---------------------------------------------------------------------
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

  // getState payloads are small (~1-2KB); 4KB leaves headroom for long
  // titles / library paths without overrunning the buffer.
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

// ---------------------------------------------------------------------
// Transport/volume commands
// ---------------------------------------------------------------------
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

// ---------------------------------------------------------------------
// Web radio - Favourites lookup + station switch
// ---------------------------------------------------------------------
int volumioGetFavouriteWebRadios(WebRadioStation out[], int maxCount) {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  // NOT Volumio's generic cross-service "favourites" (the heart icon) -
  // the webradio plugin keeps its own separate "Favorite Radios" list,
  // reached by browsing into Web Radio first. Confirmed against a live
  // browse?uri=radio response: that menu's "Favorite Radios" entry carries
  // uri "radio/favourites", distinct from the top-level "favourites" uri.
  String url = baseUrl() + "/api/v1/browse?uri=radio/favourites";
  if (!http.begin(url)) return 0;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return 0;
  }

  // A few dozen entries with cover-art URLs each - JsonDocument (ArduinoJson
  // v7) grows on the heap as needed, same as getState's doc above.
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
      // A real station's uri is a playable stream URL. Anything else here
      // would be a nested category folder rather than a station - the
      // "type"/"service" fields aren't a reliable discriminator (Volumio's
      // own category folders under radio/ carry service="webradio" too,
      // see e.g. "Volumio Selection" in browse?uri=radio), but a folder's
      // uri is always an internal path like "radio/top500", never http(s).
      if (!uri.startsWith("http://") && !uri.startsWith("https://")) continue;

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

// ---------------------------------------------------------------------
// USB music - drive detection, generic folder/song browsing, queued
// playback
// ---------------------------------------------------------------------

// Percent-encodes everything except the characters that are safe to leave
// literal in a URL - crucially INCLUDING '/', since uri here is a whole
// hierarchical path (e.g. "music-library/USB/xxx/BONEY M/Unknown Album")
// that Volumio expects to receive with its slashes intact, not as one
// escaped path segment. Real folder/file names routinely contain spaces
// and other punctuation ("BONEY M", "Unknown Album", "Go!Go!Go!"), which
// break the raw GET below if sent unescaped - that's what was producing
// "no songs found here" for any folder past the first one whose name had
// a space in it.
static String urlEncode(const String &s) {
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

// ---------------------------------------------------------------------
// Streaming (SAX-style) browse-response reader.
//
// Replaces an earlier DOM-based approach (ArduinoJson's deserializeJson()
// into a JsonDocument) that had to hold Volumio's ENTIRE browse response in
// memory before we could look at any of it - fine for a folder with a
// couple dozen items, a real problem for a flat folder with hundreds or
// thousands (that response has to be received AND fully parsed before
// getting trimmed down to whatever we actually wanted to show).
//
// This reads the response as bytes arrive off the socket, keeping only the
// three fields we care about (title/uri/type) for whichever ONE item is
// currently being read, and - the actual point of doing this - stops
// reading entirely the moment it has collected maxCount items. A folder
// with 3,000 songs capped to 200 never has the other 2,800 pulled off the
// network at all, let alone parsed or held in memory. Verified against a
// battery of test cases (nested folders, mixed folder+song content,
// non-string field values, unicode escapes, multi-section lists, and the
// early-exit behavior itself) before being written here.
// ---------------------------------------------------------------------

// Blocking byte read/peek with a bounded wait - the HTTP body can arrive in
// bursts, so this is an IDLE timeout (time since the last available byte),
// same idea as HTTPClient's own setTimeout(), not a total-operation budget.
static int streamReadByte(Stream &s) {
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

// Parses a JSON string's contents, starting AFTER the opening quote
// (caller already consumed it) through and including the closing quote.
// Handles \" \\ \/ \b \f \n \r \t and \uXXXX escapes - the last one
// re-encoded as UTF-8 (covers the full Basic Multilingual Plane, i.e. every
// real-world song/artist title; doesn't bother with surrogate pairs for
// astral characters, which won't come up here).
static String streamParseStringBody(Stream &s) {
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
  return out;  // hit EOF before the closing quote - truncated, best effort
}

// Skips one complete JSON value (string/number/true/false/null/object/
// array) we don't care about - e.g. icon, service, albumart, nested
// artwork objects - so position tracking stays correct without needing to
// know what those fields look like. Bracket depth only needs to track ONE
// bracket type (matching whichever one opened this value) because valid
// JSON never lets '{'/'}' and '['/']' cross-match - any of the other
// bracket type encountered inside is necessarily balanced on its own and
// can just be ignored.
static void streamSkipValue(Stream &s) {
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

// Consumes a value we WANT as a string - but checks first, and falls back
// to streamSkipValue() if it turns out not to actually be one (null, a
// number, ...), rather than assuming and desyncing the whole parse on a
// field that isn't always a string in practice.
static String streamConsumeStringValue(Stream &s) {
  streamSkipWhitespace(s);
  if (streamPeekByte(s) != '"') {
    streamSkipValue(s);
    return "";
  }
  streamReadByte(s);
  return streamParseStringBody(s);
}

// Call once right after an object's opening '{', then again after handling
// each key's value. Either reads the next key (positions the stream right
// after its ':', returns true) or consumes the closing '}' and returns
// false - handles empty objects and the comma between entries without the
// caller needing to track that separately.
static bool streamObjectNext(Stream &s, bool &first, String &keyOut) {
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

// Same idea as streamObjectNext(), for arrays - true positions the stream
// at the start of the next element (caller reads it), false means the
// closing ']' was just consumed.
static bool streamArrayNext(Stream &s, bool &first) {
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

// Walks navigation.lists[*].items[*], keeping up to maxCount items and
// stopping there - see the block comment above. requireKnownType=true
// (USB browsing proper) keeps only "folder"/"song" typed items;
// requireKnownType=false (the drive-presence check) keeps the very first
// item regardless of type, since a mounted drive's own listing entry isn't
// necessarily typed "folder".
static int streamParseBrowseItems(Stream &s, WebRadioStation out[], int maxCount, bool requireKnownType) {
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
              if (count >= maxCount) return count;  // the whole point - stop reading, don't just stop keeping
            }
          }
        }
      }
    }
  }

  return count;
}

// Wraps the GET + stream hookup around streamParseBrowseItems() above.
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
  http.end();  // safe even after an early exit mid-response - just closes the socket
  return count;
}

bool volumioGetUsbRootUri(String &uriOut) {
  // music-library/USB lists whatever drive(s) are mounted there. Empty
  // items = nothing plugged in - confirmed live, there's no separate flag
  // for this. Just take the first mounted drive; multiple simultaneous USB
  // drives aren't handled specially here. maxCount=1 - the stream stops
  // reading right after the first item either way.
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

  // Same replaceAndPlay shape as the REST API docs' queue-a-whole-list
  // example: list + index + item, so the rest of the folder plays on after
  // the tapped track - a real "tap a song in an album" experience rather
  // than a single track that stops.
  JsonDocument doc;
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

// Revision 2 - STILL not fully confirmed against a live Volumio, but this
// version specifically targets a bug found by testing revision 1.
//
// Revision 1 sent a bare `item` (no `list`), same as this function's
// current shape minus the `list`/`index` fields below. Testing showed the
// recursion itself works - MPD does add a directory's contents to the
// queue recursively, confirmed by it finding a track nested two levels
// down (folder/subfolder/song.mp3) - but playback didn't stop at the end
// of that folder's own songs. It rolled on into sibling folders at the
// same level. That matches Volumio falling back to auto-building the queue
// from the tapped uri's *browsing context* (i.e. its parent listing) when
// there's no explicit `list` to scope it to - which is exactly what
// volumioPlayUsbSong() above deliberately relies on for songs (its comment:
// "the rest of the folder plays on after the tapped track"), just not what
// we want here, where the folder itself should BE the whole queue.
//
// Fix attempt: wrap the folder in an explicit single-entry `list` (+
// index 0) instead of a bare `item`, mirroring volumioPlayUsbSong()'s
// working shape exactly. The theory is that an explicit `list` - even a
// one-entry one - gets treated as the complete intended queue, so once
// MPD's recursive expansion of that one folder is exhausted, playback
// should stop there rather than reaching past it into siblings.
bool volumioPlayUsbFolder(const WebRadioStation &folder) {
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

// ---------------------------------------------------------------------
// Playlists
// ---------------------------------------------------------------------

// listplaylists' response shape isn't documented with a concrete example
// anywhere in Volumio's own REST API docs (checked both
// volumio.github.io/docs and developers.volumio.com - both describe the
// endpoint but neither shows a sample response) - confirmed live instead:
// it's a flat JSON array of playlist name strings (e.g. ["Rock","Chill"]),
// NOT the {navigation:{lists:[...]}} shape every browse-family endpoint
// above uses. Simple enough to walk directly with the same low-level
// stream primitives streamParseBrowseItems() uses (no nested
// navigation.lists[].items[] to descend through, just one array of
// strings) - reusing them here rather than a JsonDocument DOM parse keeps
// this bounded the same way USB browsing is: an end user with a few
// hundred saved playlists never has that whole response held in memory at
// once, and reading stops the instant maxCount is reached rather than
// parsing everything first and truncating after.
static int streamParseNameArray(Stream &s, WebRadioStation out[], int maxCount) {
  streamSkipWhitespace(s);
  if (streamReadByte(s) != '[') return 0;  // not a flat array - the shape assumption above was wrong

  int count = 0;
  bool first = true;
  while (streamArrayNext(s, first)) {
    String name = streamConsumeStringValue(s);
    if (name.length() == 0) continue;
    out[count].name = name;
    out[count].uri = "";  // unused - volumioPlayPlaylist() takes the name directly
    count++;
    if (count >= maxCount) return count;  // stop reading entirely - the whole point, see streamParseBrowseItems()'s comment above
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
  http.end();  // safe even after an early exit mid-response - just closes the socket
  return count;
}

bool volumioPlayPlaylist(const String &name) {
  // Same /commands endpoint as play/pause/volume/etc. - playplaylist always
  // answers HTTP 200 "success" even for a name that doesn't exist (reported
  // against a live Volumio on their community forum), so a 200 here only
  // confirms the request was accepted, not that playback actually started.
  return volumioCommand("playplaylist&name=" + urlEncode(name));
}

// Lists the songs inside one playlist - Volumio's own web UI lets you
// navigate into a playlist and start from any track, same as browsing into
// a USB folder, so this mirrors that: "playlists" is itself a browsable
// uri (confirmed in the top-level browse listing, same as "radio" or
// "music-library/USB"), and "playlists/<name>" drills into one - reusing
// the same generic streaming browse parser USB uses rather than assuming
// playlists need their own. Playlists are expected to be flat (no folders
// nested inside one), but requireKnownType=true handles a folder turning
// up anyway the same safe way USB browsing does, rather than assuming.
int volumioBrowsePlaylist(const String &name, WebRadioStation out[], int maxCount) {
  return browseUriStreaming("playlists/" + name, out, maxCount, true);
}

// ---------------------------------------------------------------------
// Settings-page "Test" button
// ---------------------------------------------------------------------
bool volumioTestHost(const String &host) {
  String trimmed = host;
  trimmed.trim();
  if (trimmed.length() == 0) return false;

  HTTPClient http;
  http.setTimeout(3000);  // short and snappy - this is a UI click, not background polling
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

  // Any web server could answer 200 on that address - "status" is a field
  // real Volumio getState replies always carry ("play"/"pause"/"stop"),
  // so this is a cheap way to confirm it's actually Volumio.
  String status = doc["status"] | "";
  return status.length() > 0;
}
