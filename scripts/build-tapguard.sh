#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and contributors
# Builds TapGuard (ignore trackpad taps while typing) into build/TapGuard.app; --install copies it to /Applications and starts it
# at every login (user LaunchAgent, restarted if it exits). The first start asks for Accessibility permission.
set -euo pipefail
cd "$(dirname "$0")/.."
APP=build/TapGuard.app
rm -rf "$APP"; mkdir -p "$APP/Contents/MacOS"
cp macos/TapGuard/Info.plist "$APP/Contents/Info.plist"
swiftc -O -target x86_64-apple-macos13.0 macos/TapGuard/TapGuard.swift -o "$APP/Contents/MacOS/TapGuard"
codesign --force --sign - --identifier com.itlmt7925.TapGuard "$APP" >/dev/null
echo "built $APP"
if [ "${1:-}" = "--install" ]; then
  LA="$HOME/Library/LaunchAgents/com.itlmt7925.TapGuard.plist"
  launchctl bootout "gui/$(id -u)/com.itlmt7925.TapGuard" 2>/dev/null || true
  rm -rf /Applications/TapGuard.app; cp -R "$APP" /Applications/
  mkdir -p "$(dirname "$LA")"
  /usr/bin/python3 -I - "$LA" <<'PY'
import plistlib, sys
plistlib.dump({"Label": "com.itlmt7925.TapGuard", "ProgramArguments": ["/Applications/TapGuard.app/Contents/MacOS/TapGuard"],
               "RunAtLoad": True, "KeepAlive": True, "ProcessType": "Interactive", "LimitLoadToSessionType": "Aqua"},
              open(sys.argv[1], "wb"))
PY
  launchctl bootstrap "gui/$(id -u)" "$LA"
  echo "installed /Applications/TapGuard.app and started it. Allow it in System Settings > Privacy & Security > Accessibility."
fi
