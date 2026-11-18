#!/usr/bin/env bash
# Build StarField.saver (universal: Apple silicon + Intel) on macOS.
#
#   macos/build.sh            -> build/macos/StarField.saver
#   macos/build.sh --install  -> also copy it into ~/Library/Screen Savers
#
# Requires the Xcode command line tools (xcode-select --install).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/macos"
SAVER="$OUT/StarField.saver"
VERSION="${STARFIELD_VERSION:-1.0.0}"
ARCHS=(-arch arm64 -arch x86_64)
MIN=-mmacosx-version-min=11.0

rm -rf "$SAVER"
mkdir -p "$SAVER/Contents/MacOS" "$SAVER/Contents/Resources" "$OUT/obj"

clang -std=c99 -O2 -Wall -Wextra "${ARCHS[@]}" $MIN \
  -c "$ROOT/core/starfield.c" -o "$OUT/obj/starfield.o"

clang -fobjc-arc -O2 -Wall -Wextra -Wno-unused-parameter "${ARCHS[@]}" $MIN \
  -I"$ROOT/core" \
  -c "$ROOT/macos/StarFieldView.m" -o "$OUT/obj/StarFieldView.o"

clang -bundle "${ARCHS[@]}" $MIN \
  -framework ScreenSaver -framework Cocoa -framework QuartzCore \
  "$OUT/obj/starfield.o" "$OUT/obj/StarFieldView.o" \
  -o "$SAVER/Contents/MacOS/StarField"

sed "s/@VERSION@/$VERSION/g" "$ROOT/macos/Info.plist" > "$SAVER/Contents/Info.plist"
cp "$ROOT/assets/thumbnail.png" "$ROOT/assets/thumbnail@2x.png" "$SAVER/Contents/Resources/"

# Ad-hoc signature: required for arm64 code to load at all. Not notarized.
codesign --force --sign - "$SAVER"

echo "Built $SAVER"

if [[ "${1:-}" == "--install" ]]; then
  DEST="$HOME/Library/Screen Savers"
  mkdir -p "$DEST"
  rm -rf "$DEST/StarField.saver"
  cp -R "$SAVER" "$DEST/"
  echo "Installed to $DEST/StarField.saver — choose it in System Settings > Screen Saver."
fi
