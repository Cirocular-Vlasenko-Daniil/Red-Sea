#!/bin/sh
# Пересобрать скриншоты экранов в docs/screens из текущего REDSEA.ino.
# Нужны clang++ (или g++), python3 и библиотека Adafruit_GFX (путь — GFX=...).
set -e
cd "$(dirname "$0")"
GFX="${GFX:-$HOME/Documents/Arduino/libraries/Adafruit_GFX_Library}"
OUT=../../docs/screens
BUILD="$(mktemp -d)"
${CXX:-clang++} -std=gnu++17 -w -DARDUINO=10800 -I stubs -I "$GFX" main.cpp "$GFX/Adafruit_GFX.cpp" -o "$BUILD/render"
mkdir -p "$OUT"
"$BUILD/render" "$BUILD"
python3 pbm2png.py "$BUILD"/*.pbm
mv "$BUILD"/*.png "$OUT"/
rm -rf "$BUILD"
echo "screens: $(ls "$OUT" | wc -l | tr -d ' ') → docs/screens"
