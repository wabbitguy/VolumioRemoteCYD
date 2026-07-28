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
// USB music - drive detection, folder/song listing, queued playback
// ---------------------------------------------------------------------

// Small shared GET+parse helper - every USB browse call below is the same
// shape (hit /api/v1/browse?uri=..., parse the JSON, hand back the doc).
// Returns false (doc left however deserializeJson left it) on any failure.
static bool browseUri(const String &uri, JsonDocument &doc) {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  String url = baseUrl() + "/api/v1/browse?uri=" + uri;
  if (!http.begin(url)) return false;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  return !err;
}

int volumioListUsbFolders(WebRadioStation out[], int maxCount) {
  // Step 1: music-library/USB lists whatever drive(s) are mounted there.
  // Empty items = nothing plugged in - confirmed live, there's no separate
  // flag for this. Just take the first mounted drive; multiple simultaneous
  // USB drives aren't handled specially here.
  JsonDocument driveDoc;
  if (!browseUri("music-library/USB", driveDoc)) return 0;

  String driveUri;
  JsonArray driveLists = driveDoc["navigation"]["lists"].as<JsonArray>();
  for (JsonObject list : driveLists) {
    JsonArray items = list["items"].as<JsonArray>();
    for (JsonObject item : items) {
      driveUri = item["uri"] | "";
      break;
    }
    if (driveUri.length() > 0) break;
  }
  if (driveUri.length() == 0) return 0;  // nothing mounted

  // Step 2: list the folders at that drive's root.
  JsonDocument doc;
  if (!browseUri(driveUri, doc)) return 0;

  JsonArray lists = doc["navigation"]["lists"].as<JsonArray>();
  int count = 0;
  for (JsonObject list : lists) {
    JsonArray items = list["items"].as<JsonArray>();
    for (JsonObject item : items) {
      if (count >= maxCount) return count;

      String type = item["type"] | "";
      if (type != "folder") continue;  // skip any stray loose files sitting at the drive root

      String uri = item["uri"] | "";
      if (uri.length() == 0) continue;
      String name = item["title"] | "";
      if (name.length() == 0) name = uri;

      out[count].name = name;
      out[count].uri = uri;
      count++;
    }
  }
  return count;
}

int volumioListUsbSongs(const String &folderUri, WebRadioStation out[], int maxCount) {
  JsonDocument doc;
  if (!browseUri(folderUri, doc)) return 0;

  JsonArray lists = doc["navigation"]["lists"].as<JsonArray>();
  int count = 0;
  for (JsonObject list : lists) {
    JsonArray items = list["items"].as<JsonArray>();
    for (JsonObject item : items) {
      if (count >= maxCount) return count;

      String type = item["type"] | "";
      if (type != "song") continue;  // flat folder assumed - no sub-folder recursion

      String uri = item["uri"] | "";
      if (uri.length() == 0) continue;
      String name = item["title"] | "";
      if (name.length() == 0) name = uri;

      out[count].name = name;
      out[count].uri = uri;
      count++;
    }
  }
  return count;
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
