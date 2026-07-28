#pragma once
#include <Arduino.h>
#include "volumio_api.h"

// ---------------------------------------------------------------------
// Album art rendering (JPEG fetch/decode + Discogs/iTunes fallback chain)
// ---------------------------------------------------------------------
void artworkInit();  // prepares the JPEG decoder - call once from setup()

// Fetches and draws album art for state at (x,y), sized size x size. Falls
// back to Discogs/iTunes when Volumio only has a generic station icon
// (typical for webradio). Returns true if something was drawn.
bool artworkRender(const VolumioState &state, int x, int y, int size);

// ---------------------------------------------------------------------
// Button icons (LittleFS JPEGs, manually resampled - TJpg_Decoder only
// scales by powers of two)
// ---------------------------------------------------------------------

// Draws a srcSize x srcSize JPEG from `path`, resampled to dstSize x dstSize.
bool artworkDrawIcon(const char *path, int x, int y, int srcSize, int dstSize);

// Same icon, color-inverted - touch-down feedback. artworkDrawIcon() restores
// it on release. srcSize/dstSize split the same way as artworkDrawIcon() -
// needed whenever the on-screen button size doesn't match the source JPEG's
// native size (e.g. icons shrunk to fit more buttons in the row).
bool artworkDrawIconPressed(const char *path, int x, int y, int srcSize, int dstSize);
