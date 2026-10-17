#!/bin/bash
# Compile Burger Rush pour iOS (sur un Mac avec Xcode).
#
# raylib n'a pas de version iOS officielle : on utilise son backend SDL2
# (fenetre, OpenGL ES 2 et tactile geres par SDL).
#
# Usage : scripts/build_ios.sh device|simulator
#   RAYLIB_SRC : sources de raylib 5.5
#   SDL_SRC    : sources de SDL 2.30
# Resultat : build-ios-<cible>/BurgerRush.app (+ BurgerRush-iOS.ipa pour "device")
set -euo pipefail

TARGET=${1:-device}
OUT=${OUT:-build-ios-$TARGET}
RAYLIB=${RAYLIB_SRC:?RAYLIB_SRC manquant}
SDLSRC=${SDL_SRC:?SDL_SRC manquant}
case $TARGET in
    device)    SDK=iphoneos;        TRIPLE=arm64-apple-ios13.0;           PLATFORM=iPhoneOS ;;
    simulator) SDK=iphonesimulator; TRIPLE=arm64-apple-ios13.0-simulator; PLATFORM=iPhoneSimulator ;;
    *) echo "cible inconnue : $TARGET"; exit 1 ;;
esac
SYSROOT=$(xcrun --sdk $SDK --show-sdk-path)
CC="xcrun --sdk $SDK clang -target $TRIPLE -isysroot $SYSROOT"
mkdir -p "$OUT/obj"

echo "=== SDL2 ($TARGET)"
cmake -S "$SDLSRC" -B "$OUT/sdl-build" -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=$SDK \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DCMAKE_BUILD_TYPE=Release \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DCMAKE_INSTALL_PREFIX="$PWD/$OUT/sdl"
cmake --build "$OUT/sdl-build" --config Release -j 4
cmake --install "$OUT/sdl-build" --config Release
SDLINC="$OUT/sdl/include/SDL2"

echo "=== raylib (backend SDL, OpenGL ES 2)"
RLFLAGS="-std=gnu99 -O2 -DPLATFORM_DESKTOP_SDL -DGRAPHICS_API_OPENGL_ES2 -I$RAYLIB/src -I$SDLINC"
for f in rcore rshapes rtextures rtext rmodels utils; do
    $CC $RLFLAGS -c "$RAYLIB/src/$f.c" -o "$OUT/obj/rl_$f.o"
done
$CC $RLFLAGS -x objective-c -c "$RAYLIB/src/raudio.c" -o "$OUT/obj/rl_raudio.o"   # miniaudio : AVAudioSession

echo "=== jeu"
for f in src/*.c ios/platform_ios.c; do
    $CC -std=gnu99 -O2 -Wall -DBR_MOBILE -DBR_IOS -I"$RAYLIB/src" -I"$SDLINC" -c "$f" -o "$OUT/obj/$(basename "$f" .c).o"
done

echo "=== BurgerRush.app"
APP="$OUT/BurgerRush.app"
rm -rf "$APP"
mkdir -p "$APP"
$CC -o "$APP/BurgerRush" "$OUT"/obj/*.o "$OUT/sdl/lib/libSDL2.a" "$OUT/sdl/lib/libSDL2main.a" \
    -framework UIKit -framework Foundation -framework OpenGLES -framework QuartzCore -framework CoreGraphics \
    -framework CoreMotion -framework GameController -framework AVFoundation -framework AudioToolbox \
    -framework CoreAudio -framework CoreHaptics -framework Metal -framework CoreBluetooth -framework CoreVideo \
    -liconv
sed "s/__PLATFORM__/$PLATFORM/" ios/Info.plist > "$APP/Info.plist"
cp ios/icons/*.png "$APP/"
codesign --force --sign - "$APP"   # signature "ad hoc" (simulateur ; sur iPhone, l'outil d'installation re-signe)

if [ "$TARGET" = device ]; then
    rm -rf "$OUT/Payload" && mkdir -p "$OUT/Payload" && cp -R "$APP" "$OUT/Payload/"
    (cd "$OUT" && rm -f BurgerRush-iOS.ipa && zip -qry BurgerRush-iOS.ipa Payload)
    ls -la "$OUT/BurgerRush-iOS.ipa"
fi
file "$APP/BurgerRush"
