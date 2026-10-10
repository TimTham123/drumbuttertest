#!/bin/bash
# Installs the DrumSpread VST3 and AU for the current user (no admin needed).
cd "$(dirname "$0")" || exit 1
set -e
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"
rm -rf "$VST3_DIR/DrumSpread.vst3" "$AU_DIR/DrumSpread.component"
cp -R DrumSpread.vst3 "$VST3_DIR/"
cp -R DrumSpread.component "$AU_DIR/"
xattr -dr com.apple.quarantine "$VST3_DIR/DrumSpread.vst3" 2>/dev/null || true
xattr -dr com.apple.quarantine "$AU_DIR/DrumSpread.component" 2>/dev/null || true
xattr -dr com.apple.quarantine DrumSpread.app 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo
echo "DrumSpread installed:"
echo "  $VST3_DIR/DrumSpread.vst3"
echo "  $AU_DIR/DrumSpread.component"
echo "Restart your DAW and rescan plugins."
echo
read -n 1 -s -r -p "Press any key to close."
