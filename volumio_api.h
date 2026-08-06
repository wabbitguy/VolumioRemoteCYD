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
// favorites and USB browsing reuse this same {name, uri} shape, so one
// struct and one array-size cap cover the whole picker. isFolder is only
// meaningful for USB browsing (false/unused for favorites).
// ---------------------------------------------------------------------
// One shared cap for: favorites, and one USB browse level's folders+songs
// combined. Never more than one of these is "live" (in stationList[]) at a
// time, so one number covers all of them. Raised from the original 80 -
// that was sized off "~54 tracks in one observed folder" but a real
// top-level USB listing (e.g. one folder per artist) can easily be larger,
// and folders+songs at a level now share this same array.
static const int MAX_STATIONS = 200;

struct WebRadioStation {
  String name;
  String uri;
  bool isFolder = false;  // USB browsing only: true = drill in further, false = playable song
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
// list = nothing plugged in - there's no separate "is USB mounted" flag).
//
// Below the drive root, real USB libraries can be nested arbitrarily deep
// (USB/<artist>/<album>/<song>.mp3 is the common case, but any depth is
// possible) - volumioBrowseUsb() is a single generic "list what's at this
// uri" call rather than the old two-fixed-levels split, so the .ino can
// walk down (and back up) as many levels as the drive actually has.
// ---------------------------------------------------------------------

// "Is a USB drive mounted" check - true + the drive's root browse uri if
// so, false if nothing's plugged in. Cheap: one browse call, no listing of
// its contents (that's volumioBrowseUsb(), called separately once the user
// actually picks USB Music).
bool volumioGetUsbRootUri(String &uriOut);

// Lists what's directly at one USB browse uri (the drive root, or any
// folder reached by drilling into it) - both sub-folders and playable
// songs, in whatever order Volumio returns them, with out[i].isFolder
// marking which is which. Returns how many entries were found (0 = empty
// folder/drive).
int volumioBrowseUsb(const String &uri, WebRadioStation out[], int maxCount);

// Plays one song from an already-fetched song list (song-only entries, as
// filtered from a volumioBrowseUsb() result), queuing the rest of that same
// list to play afterward - startIndex is which song to start at, same "tap
// a track in an album, the rest plays after it" behavior as a real music
// player.
bool volumioPlayUsbSong(WebRadioStation songs[], int songCount, int startIndex);

// Plays every song inside a USB folder, INCLUDING nested subfolders,
// without the ESP32 having to walk the tree itself - relies on Volumio/MPD
// resolving a directory uri recursively server-side (MPD's queue-add is
// inherently recursive when given a folder rather than a single file),
// the same mechanism Volumio's own web UI's per-row Play button uses.
//
// STILL NOT FULLY CONFIRMED - see the revision-2 comment above this
// function's definition in volumio_api.cpp for what live testing has shown
// so far. Confirmed: MPD's recursive queue-add does work (a track nested
// two folders down was found and played). Not yet confirmed: revision 2's
// fix for the follow-on bug that testing found, where playback rolled past
// the end of the folder into sibling folders at the same level instead of
// stopping. If revision 2 still doesn't stop cleanly at the folder
// boundary, the next step is to capture the actual request Volumio's own
// web UI sends (browser DevTools -> Network tab -> click Play on a folder
// -> inspect the POST body) rather than guessing further blind.
bool volumioPlayUsbFolder(const WebRadioStation &folder);
