#!/bin/sh
# Emballe l'executable macOS dans une application ZeffalieComeBack.app puis la zippe.
# Usage : sh scripts/make_macos_app.sh <executable> <dossier de sortie>
# set -e : arret a la premiere erreur
set -e

# Arguments : executable compile, et dossier de sortie ("dist" par defaut)
BIN="$1"
OUT="${2:-dist}"
APP="$OUT/ZeffalieComeBack.app"

# Structure minimale d'une application macOS :
#   ZeffalieComeBack.app/Contents/Info.plist        description de l'application
#   ZeffalieComeBack.app/Contents/MacOS/ZeffalieComeBack  l'executable
#   ZeffalieComeBack.app/Contents/Resources/        ressources (l'icone AppIcon.icns)
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/ZeffalieComeBack"
chmod +x "$APP/Contents/MacOS/ZeffalieComeBack"
# Icone (poussin enchaine), generee par scripts/make_icon.py
cp assets/icon.icns "$APP/Contents/Resources/AppIcon.icns"

# Info.plist ecrit via un "here-document" (tout jusqu'a la ligne PLIST) :
# nom affiche, executable, identifiant unique, version, macOS 11 minimum, support Retina
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Zeffalie Come Back</string>
    <key>CFBundleDisplayName</key><string>Zeffalie Come Back</string>
    <key>CFBundleExecutable</key><string>ZeffalieComeBack</string>
    <key>CFBundleIdentifier</key><string>io.github.wakadu13.zeffaliecomeback</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
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
(cd "$OUT" && rm -f ZeffalieComeBack-macOS-arm64.zip && ditto -c -k --keepParent ZeffalieComeBack.app ZeffalieComeBack-macOS-arm64.zip)
echo "Cree : $OUT/ZeffalieComeBack-macOS-arm64.zip"
