#!/bin/bash
# Builds TufControl (FX517ZM adaptation of RogControl's design) into build/TufControl.app. Ad-hoc signed.
#   scripts/build-tuf-control.sh            build
#   scripts/build-tuf-control.sh --install  build, replace /Applications/TufControl.app and open it (no login agent;
#                                           "Open at Login" is a user toggle inside the app)
set -euo pipefail
cd "$(dirname "$0")/.."
APP=build/TufControl.app
rm -rf "$APP"; mkdir -p "$APP/Contents/MacOS"
cp macos/TufControl/Info.plist "$APP/Contents/Info.plist"
swiftc -O -target x86_64-apple-macos13.0 macos/TufControl/TufControl.swift -o "$APP/Contents/MacOS/TufControl" -framework IOKit -framework SwiftUI
codesign --force --sign - "$APP"
codesign --verify --strict "$APP"
echo "Built $APP (no login agent or automatic firmware writes)."
if [ "${1:-}" = "--install" ]; then
  pkill -x TufControl 2>/dev/null || true
  rm -rf /Applications/TufControl.app
  cp -R "$APP" /Applications/
  open /Applications/TufControl.app
  echo "Installed /Applications/TufControl.app"
fi
