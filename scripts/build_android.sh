#!/bin/bash
# Compile l'APK Android de Burger Rush, sans Gradle.
#
# Prerequis (deja installes sur les machines GitHub Actions) :
#   ANDROID_HOME  : SDK Android (build-tools + platforms)
#   ANDROID_NDK   : NDK Android
#   RAYLIB_SRC    : sources de raylib 5.5
# Resultat : $OUT/BurgerRush-android.apk (arm64-v8a pour les telephones, x86_64 pour les emulateurs)
set -euo pipefail

API=24
OUT=${OUT:-build-android}
NDK=${ANDROID_NDK:?ANDROID_NDK manquant}
SDK=${ANDROID_HOME:?ANDROID_HOME manquant}
RAYLIB=${RAYLIB_SRC:?RAYLIB_SRC manquant}
BT=$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)
PLATFORM_JAR=$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)/android.jar
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
echo "build-tools : $BT"
echo "plateforme  : $PLATFORM_JAR"

rm -rf "$OUT"
mkdir -p "$OUT/apk"

for ABI in arm64-v8a x86_64; do
    case $ABI in
        arm64-v8a) ARCH=arm64;  TRIPLE=aarch64-linux-android ;;
        x86_64)    ARCH=x86_64; TRIPLE=x86_64-linux-android ;;
    esac
    echo "=== raylib pour $ABI"
    make -C "$RAYLIB/src" clean > /dev/null
    make -C "$RAYLIB/src" PLATFORM=PLATFORM_ANDROID ANDROID_NDK="$NDK" ANDROID_ARCH=$ARCH ANDROID_API_VERSION=$API -j4

    echo "=== jeu pour $ABI"
    mkdir -p "$OUT/apk/lib/$ABI"
    "$TOOLCHAIN/bin/${TRIPLE}${API}-clang" -std=c99 -O2 -fPIC -shared -Wall \
        -DBR_MOBILE -DPLATFORM_ANDROID \
        -I"$RAYLIB/src" -I"$NDK/sources/android/native_app_glue" \
        src/*.c -o "$OUT/apk/lib/$ABI/libmain.so" \
        -L"$RAYLIB/src" -lraylib -llog -landroid -lEGL -lGLESv2 -lOpenSLES -lm \
        -u ANativeActivity_onCreate -Wl,--no-undefined
done

echo "=== APK"
"$BT/aapt" package -f -M android/AndroidManifest.xml -S android/res -I "$PLATFORM_JAR" -F "$OUT/unaligned.apk"
(cd "$OUT/apk" && "$BT/aapt" add ../unaligned.apk lib/arm64-v8a/libmain.so lib/x86_64/libmain.so)
"$BT/zipalign" -f -p 4 "$OUT/unaligned.apk" "$OUT/aligned.apk"
# Cle de debug commitee dans le depot : les mises a jour s'installent par-dessus l'ancienne version
"$BT/apksigner" sign --ks android/debug.keystore --ks-pass pass:android --key-pass pass:android \
    --out "$OUT/BurgerRush-android.apk" "$OUT/aligned.apk"
"$BT/apksigner" verify "$OUT/BurgerRush-android.apk"
ls -la "$OUT/BurgerRush-android.apk"
