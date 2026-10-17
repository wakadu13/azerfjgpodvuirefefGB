#!/bin/sh
# Emballe l'executable macOS dans une application BurgerRush.app puis la zippe.
# Usage : sh scripts/make_macos_app.sh <executable> <dossier de sortie>
set -e

BIN="$1"
OUT="${2:-dist}"
APP="$OUT/BurgerRush.app"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/BurgerRush"
chmod +x "$APP/Contents/MacOS/BurgerRush"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Burger Rush</string>
    <key>CFBundleDisplayName</key><string>Burger Rush</string>
    <key>CFBundleExecutable</key><string>BurgerRush</string>
    <key>CFBundleIdentifier</key><string>io.github.wakadu13.burgerrush</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundleVersion</key><string>1</string>
    <key>LSMinimumSystemVersion</key><string>11.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

# Signature "ad hoc" : obligatoire pour lancer un binaire ARM sur macOS
codesign --force --deep --sign - "$APP"

(cd "$OUT" && rm -f BurgerRush-macOS-arm64.zip && ditto -c -k --keepParent BurgerRush.app BurgerRush-macOS-arm64.zip)
echo "Cree : $OUT/BurgerRush-macOS-arm64.zip"
