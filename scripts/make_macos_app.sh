#!/bin/sh
# Emballe l'executable macOS dans une application BurgerRush.app puis la zippe.
# Usage : sh scripts/make_macos_app.sh <executable> <dossier de sortie>
# set -e : arret a la premiere erreur
set -e

# Arguments : executable compile, et dossier de sortie ("dist" par defaut)
BIN="$1"
OUT="${2:-dist}"
APP="$OUT/BurgerRush.app"

# Structure minimale d'une application macOS :
#   BurgerRush.app/Contents/Info.plist        description de l'application
#   BurgerRush.app/Contents/MacOS/BurgerRush  l'executable
#   BurgerRush.app/Contents/Resources/        ressources (vide ici)
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/BurgerRush"
chmod +x "$APP/Contents/MacOS/BurgerRush"

# Info.plist ecrit via un "here-document" (tout jusqu'a la ligne PLIST) :
# nom affiche, executable, identifiant unique, version, macOS 11 minimum, support Retina
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

# ditto -c -k --keepParent : zip compatible Finder qui conserve le dossier .app
# (et ses attributs / signature) a l'interieur de l'archive
(cd "$OUT" && rm -f BurgerRush-macOS-arm64.zip && ditto -c -k --keepParent BurgerRush.app BurgerRush-macOS-arm64.zip)
echo "Cree : $OUT/BurgerRush-macOS-arm64.zip"
