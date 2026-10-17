#!/bin/bash
# Compile Zeffalie Come Back pour iOS (sur un Mac avec Xcode).
#
# raylib n'a pas de version iOS officielle : on utilise son backend SDL2
# (fenetre, OpenGL ES 2 et tactile geres par SDL).
#
# Usage : scripts/build_ios.sh device|simulator
#   RAYLIB_SRC : sources de raylib 5.5
#   SDL_SRC    : sources de SDL 2.30
# Resultat : build-ios-<cible>/ZeffalieComeBack.app (+ ZeffalieComeBack-iOS.ipa pour "device")
#
# Etapes : 1. SDL2 en bibliotheque statique (CMake) ; 2. raylib avec le backend SDL ;
# 3. le jeu ; 4. edition de liens + paquet .app (Info.plist, icones, signature) ;
# 5. pour un vrai iPhone, archive .ipa (dossier Payload/ zippe).
set -euo pipefail

# Cible : premier argument du script, "device" par defaut
TARGET=${1:-device}
OUT=${OUT:-build-ios-$TARGET}
RAYLIB=${RAYLIB_SRC:?RAYLIB_SRC manquant}
SDLSRC=${SDL_SRC:?SDL_SRC manquant}
# Selon la cible : SDK Xcode a utiliser, triplet de compilation (arm64, iOS 13 minimum)
# et nom de plateforme ecrit dans l'Info.plist
case $TARGET in
    device)    SDK=iphoneos;        TRIPLE=arm64-apple-ios13.0;           PLATFORM=iPhoneOS ;;
    simulator) SDK=iphonesimulator; TRIPLE=arm64-apple-ios13.0-simulator; PLATFORM=iPhoneSimulator ;;
    *) echo "cible inconnue : $TARGET"; exit 1 ;;
esac
# Chemin du SDK iOS et commande de compilation commune (clang d'Xcode pour la bonne cible)
SYSROOT=$(xcrun --sdk $SDK --show-sdk-path)
CC="xcrun --sdk $SDK clang -target $TRIPLE -isysroot $SYSROOT"
mkdir -p "$OUT/obj"

# 1. SDL2 : configuration CMake pour iOS, bibliotheque statique uniquement (pas de tests),
# installee dans $OUT/sdl (en-tetes + libSDL2.a + libSDL2main.a)
echo "=== SDL2 ($TARGET)"
cmake -S "$SDLSRC" -B "$OUT/sdl-build" -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=$SDK \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DCMAKE_BUILD_TYPE=Release \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DCMAKE_INSTALL_PREFIX="$PWD/$OUT/sdl"
cmake --build "$OUT/sdl-build" --config Release -j 4
cmake --install "$OUT/sdl-build" --config Release
SDLINC="$OUT/sdl/include/SDL2"

echo "=== raylib (backend SDL, OpenGL ES 2)"
# 2. raylib : PLATFORM_DESKTOP_SDL = backend SDL (qui fonctionne aussi sur iOS), rendu OpenGL ES 2
RLFLAGS="-std=gnu99 -O2 -DPLATFORM_DESKTOP_SDL -DGRAPHICS_API_OPENGL_ES2 -I$RAYLIB/src -I$SDLINC"
for f in rcore rshapes rtextures rtext rmodels utils; do
    $CC $RLFLAGS -c "$RAYLIB/src/$f.c" -o "$OUT/obj/rl_$f.o"
done
# raudio.c est compile en Objective-C : miniaudio a besoin d'AVAudioSession (API Objective-C) sur iOS
$CC $RLFLAGS -x objective-c -c "$RAYLIB/src/raudio.c" -o "$OUT/obj/rl_raudio.o"   # miniaudio : AVAudioSession

# 3. Le jeu : BR_MOBILE (controles tactiles) et BR_IOS (code propre a iOS, ex. IosScreenSize)
echo "=== jeu"
for f in src/*.c ios/platform_ios.c; do
    $CC -std=gnu99 -O2 -Wall -DBR_MOBILE -DBR_IOS -I"$RAYLIB/src" -I"$SDLINC" -c "$f" -o "$OUT/obj/$(basename "$f" .c).o"
done

# 4. Paquet .app : un dossier contenant l'executable, l'Info.plist et les icones
echo "=== ZeffalieComeBack.app"
APP="$OUT/ZeffalieComeBack.app"
rm -rf "$APP"
mkdir -p "$APP"
# Edition de liens avec SDL2 et les frameworks Apple dont SDL a besoin
# (affichage, son, manettes, capteurs, retour haptique...)
$CC -o "$APP/ZeffalieComeBack" "$OUT"/obj/*.o "$OUT/sdl/lib/libSDL2.a" "$OUT/sdl/lib/libSDL2main.a" \
    -framework UIKit -framework Foundation -framework OpenGLES -framework QuartzCore -framework CoreGraphics \
    -framework CoreMotion -framework GameController -framework AVFoundation -framework AudioToolbox \
    -framework CoreAudio -framework CoreHaptics -framework Metal -framework CoreBluetooth -framework CoreVideo \
    -liconv
# Info.plist : on remplace le marqueur __PLATFORM__ par iPhoneOS ou iPhoneSimulator
sed "s/__PLATFORM__/$PLATFORM/" ios/Info.plist > "$APP/Info.plist"
cp ios/icons/*.png "$APP/"
codesign --force --sign - "$APP"   # signature "ad hoc" (simulateur ; sur iPhone, l'outil d'installation re-signe)

# 5. Archive .ipa : format d'installation iOS = un zip contenant Payload/ZeffalieComeBack.app
if [ "$TARGET" = device ]; then
    rm -rf "$OUT/Payload" && mkdir -p "$OUT/Payload" && cp -R "$APP" "$OUT/Payload/"
    (cd "$OUT" && rm -f ZeffalieComeBack-iOS.ipa && zip -qry ZeffalieComeBack-iOS.ipa Payload)
    ls -la "$OUT/ZeffalieComeBack-iOS.ipa"
fi
# Affiche l'architecture de l'executable produit (verification)
file "$APP/ZeffalieComeBack"
