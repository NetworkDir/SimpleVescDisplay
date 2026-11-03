#!/bin/bash
# Builds the firmware for every supported board with arduino-cli and collects
# flashable images in ./release (flash *_merged.bin at address 0x0).
#
# Requirements: arduino-cli with the esp32 core, the libraries LovyanGFX, lvgl,
# NimBLE-Arduino and firmware/lv_conf.h copied next to the lvgl library folder.
#
#   ARDUINO_CLI=/path/to/arduino-cli tools/build_all.sh            # all boards
#   tools/build_all.sh BOARD_ESP32_3248S035C                       # one board
#
# The board is selected in a temporary copy of the sketch, so all boards share one
# build folder and the libraries are compiled only once (first build ~5 min, then ~1 min/board).
set -e
CLI=${ARDUINO_CLI:-arduino-cli}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${TMPDIR:-/tmp}/svd_release_build"
BOARDS=${*:-"BOARD_ESP32_2432S028R BOARD_ESP32_2432S028R_ST7789 BOARD_ESP32_3248S035R BOARD_ESP32_3248S035C"}
FQBN="esp32:esp32:esp32:PartitionScheme=huge_app,PSRAM=disabled"
VERSION=$(sed -n 's/.*FW_VERSION *"\(.*\)".*/\1/p' "$ROOT/firmware/SimpleVescDisplay/src/config/app_config.h")

mkdir -p "$ROOT/release" "$WORK"
for BOARD in $BOARDS; do
  NAME=$(echo "${BOARD#BOARD_}" | tr '_' '-')
  echo "=== $NAME"
  rm -rf "$WORK/sketch/SimpleVescDisplay"
  mkdir -p "$WORK/sketch"
  cp -r "$ROOT/firmware/SimpleVescDisplay" "$WORK/sketch/"
  CFG="$WORK/sketch/SimpleVescDisplay/src/config/board_config.h"
  { echo "#define $BOARD"; cat "$ROOT/firmware/SimpleVescDisplay/src/config/board_config.h"; } > "$CFG"
  "$CLI" compile --fqbn "$FQBN" --build-path "$WORK/build" --output-dir "$WORK/out/$NAME" \
    "$WORK/sketch/SimpleVescDisplay"
  cp "$WORK/out/$NAME/SimpleVescDisplay.ino.merged.bin" "$ROOT/release/SimpleVescDisplay_v${VERSION}_${NAME}_merged.bin"
  cp "$WORK/out/$NAME/SimpleVescDisplay.ino.bin" "$ROOT/release/SimpleVescDisplay_v${VERSION}_${NAME}_app.bin"
done
ls -l "$ROOT/release"
