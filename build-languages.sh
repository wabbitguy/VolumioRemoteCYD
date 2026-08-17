#!/usr/bin/env bash
# Optional helper: compiles one merged binary per language with arduino-cli
# and drops it straight into firmware/<lang>/, ready to commit.
#
# Requires arduino-cli installed and the esp32 core already added:
#   arduino-cli core install esp32:esp32
#
# Adjust SKETCH_DIR, FQBN, and the LANGS array for your project. This script
# assumes your sketch reads a LANGUAGE build flag (see README-BUILD.md,
# "Option B") -- edit the --build-property line if your setup differs.

set -euo pipefail

SKETCH_DIR="./VolumioRemoteCYD"      # folder containing the .ino file
FQBN="esp32:esp32:esp32"               # adjust if your CYD board needs a different FQBN
LANGS=(en fr de es nl pt)              # add/remove language codes here

for LANG in "${LANGS[@]}"; do
  echo "==> Building $LANG"
  OUT_DIR="./firmware/${LANG}"
  mkdir -p "$OUT_DIR"

  arduino-cli compile \
    --fqbn "$FQBN" \
    --build-property "build.extra_flags=-DLANGUAGE=LANG_${LANG^^}" \
    --export-binaries \
    --output-dir "$OUT_DIR" \
    "$SKETCH_DIR"

  # arduino-cli names the merged export after the sketch, e.g.
  # VolumioRemoteCYD.ino.merged.bin -- rename to the fixed name
  # manifest.json expects, then clean up everything else arduino-cli
  # exported (bootloader/partitions/app/elf/map/etc. -- not needed since
  # merged.bin already contains what we need).
  cd "$OUT_DIR"
  for f in *.ino.merged.bin; do mv -f "$f" firmware.merged.bin; done
  find . -maxdepth 1 -type f ! -name 'firmware.merged.bin' ! -name 'manifest.json' -delete
  cd - >/dev/null

  if [ ! -f "$OUT_DIR/firmware.merged.bin" ]; then
    echo "WARNING: no merged.bin produced for $LANG -- update your esp32 core via Boards Manager (older cores don't export one)"
  fi

  # Reuse the manifest template if this language doesn't have one yet.
  if [ ! -f "$OUT_DIR/manifest.json" ]; then
    cp "./firmware/en/manifest.json" "$OUT_DIR/manifest.json" 2>/dev/null || true
  fi
done

echo "Done. Review firmware/<lang>/ folders, then commit & push."
