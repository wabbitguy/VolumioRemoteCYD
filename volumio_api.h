#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------
// Volumio REST API - getState polling and transport/volume commands.
// ---------------------------------------------------------------------

struct VolumioState {
  bool   valid = false;
  String status;        // "play" | "pause" | "stop"
  String title;
  String artist;
  String album;
  String albumArtUrl;   // fully-qualified URL, ready to fetch
  String service;       // "mpd", "webradio", "airplay_emulation", etc.
  int    volume = -1;
  bool   mute = false;
  int    seek = 0;       // ms
  int    duration = 0;   // s
  bool disableUiControls = false;  // Volumio's own flag: hide transport controls (e.g. AirPlay)
};

bool volumioGetState(VolumioState &out);       // fetch current state, true on success
bool volumioCommand(const String &cmd);        // "play", "pause", "toggle", "next", "prev", ...
bool volumioSetVolume(int vol);                // absolute volume, 0-100

// Quick reachability check for the settings page's "Test" button - hits
// getState on whatever host/IP the user typed (NOT the currently-saved
// host), with a short timeout, and confirms the reply actually looks like
// Volumio rather than just any web server answering on that address.
bool volumioTestHost(const String &host);

// ---------------------------------------------------------------------
// Source switcher (Web Radio Favorites / USB Music, on-device list) - both
// favorites and USB folders/songs reuse this same {name, uri} shape, so one
// struct and one pair of array-size caps cover the whole picker.
// ---------------------------------------------------------------------
static const int MAX_STATIONS = 80;      // covers favorites AND the current USB folder's songs (one observed folder has ~54 tracks) - never both at once, so one shared cap
static const int MAX_USB_FOLDERS = 20;   // top-level folders on the USB drive - plenty of headroom over the 2 seen live

struct WebRadioStation {
  String name;
  String uri;
};

// Fetches the webradio plugin's own "Favorite Radios" list (uri
// radio/favourites - distinct from Volumio's generic cross-service
// "favourites" heart-list at the top-level uri). Writes up to maxCount
// entries into out[] and returns how many were found (0 on failure or an
// empty list).
int volumioGetFavouriteWebRadios(WebRadioStation out[], int maxCount);

// Replaces the queue with a single web radio station and starts playing it -
// same replaceAndPlay call the Volumio app itself uses to switch stations.
bool volumioPlayWebRadio(const WebRadioStation &station);

// ---------------------------------------------------------------------
// USB music browsing - confirmed live against Volumio's actual browse tree:
// music-library/USB lists whatever drive(s) are mounted there (empty items
// list = nothing plugged in - there's no separate "is USB mounted" flag),
// browsing into the drive itself lists its top-level folders, and browsing
// into a folder lists its song files directly (flat, no further nesting
// assumed - matches "dump songs in a folder" USB setups).
// ---------------------------------------------------------------------

// Combines the "is a USB drive mounted" check and the folder listing into
// one call, since there's nothing to check independently - an empty result
// here means no drive is plugged in. Returns the folder count (0 = no USB).
int volumioListUsbFolders(WebRadioStation out[], int maxCount);

// Lists the song files directly inside one USB folder (folderUri as
// returned by volumioListUsbFolders). Returns the song count.
int volumioListUsbSongs(const String &folderUri, WebRadioStation out[], int maxCount);

// Plays one song from an already-fetched song list (as filled in by
// volumioListUsbSongs), queuing the rest of that same list to play
// afterward - startIndex is which song to start at, same "tap a track in
// an album, the rest plays after it" behavior as a real music player.
bool volumioPlayUsbSong(WebRadioStation songs[], int songCount, int startIndex);
