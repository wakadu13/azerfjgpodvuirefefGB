#!/bin/bash
# Test de fumee dans le simulateur iOS : installe l'app, capture le menu,
# puis relance en partie directe (BR_AUTOSTART) et capture le jeu.
# Usage : scripts/ios_smoke_test.sh <BurgerRush.app> <dossier de sortie>
set -x
APP=$1
OUT=$2
BUNDLE=io.github.wakadu13.burgerrush
mkdir -p "$OUT"

UDID=$(xcrun simctl list devices available -j | python3 -c '
import json, sys
devices = json.load(sys.stdin)["devices"]
print(next(d["udid"] for runtime, lst in devices.items() if "iOS" in runtime for d in lst if d["name"].startswith("iPhone")))')
echo "simulateur : $UDID"
xcrun simctl boot "$UDID"
xcrun simctl bootstatus "$UDID" -b
xcrun simctl install "$UDID" "$APP"

xcrun simctl launch --stdout="$PWD/$OUT/ios-stdout-menu.txt" --stderr="$PWD/$OUT/ios-stderr-menu.txt" "$UDID" $BUNDLE
sleep 25
xcrun simctl io "$UDID" screenshot "$OUT/ios-1-menu.png"
xcrun simctl terminate "$UDID" $BUNDLE

SIMCTL_CHILD_BR_AUTOSTART=1 xcrun simctl launch --stdout="$PWD/$OUT/ios-stdout-jeu.txt" --stderr="$PWD/$OUT/ios-stderr-jeu.txt" "$UDID" $BUNDLE
sleep 25
xcrun simctl io "$UDID" screenshot "$OUT/ios-2-jeu.png"
xcrun simctl spawn "$UDID" launchctl list | grep -i burger >> "$OUT/ios-stdout-jeu.txt" || echo "le processus ne tourne plus" >> "$OUT/ios-stdout-jeu.txt"
exit 0
