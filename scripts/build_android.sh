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

GLUE="$NDK/sources/android/native_app_glue"
RAYLIB_MODULES="rcore rshapes rtextures rtext rmodels raudio utils"

for ABI in arm64-v8a x86_64; do
    case $ABI in
        arm64-v8a) TRIPLE=aarch64-linux-android ;;
        x86_64)    TRIPLE=x86_64-linux-android ;;
    esac
    # Le compilateur du NDK porte la cible Android dans son nom (ex. aarch64-linux-android24-clang).
    # raylib est compile ici directement : son Makefile 5.5 ajoute "-target aarch64", qui
    # ecrase cette cible avec les NDK recents et fait inclure les en-tetes Linux du PC.
    CC="$TOOLCHAIN/bin/${TRIPLE}${API}-clang"
    OBJ="$OUT/obj-$ABI"
    mkdir -p "$OBJ" "$OUT/apk/lib/$ABI"
    COMMON="-O2 -fPIC -ffunction-sections -funwind-tables -fstack-protector-strong"

    echo "=== raylib pour $ABI"
    for m in $RAYLIB_MODULES; do
        $CC -std=gnu99 $COMMON -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2 \
            -I"$RAYLIB/src" -I"$GLUE" -c "$RAYLIB/src/$m.c" -o "$OBJ/rl_$m.o"
    done
    $CC -std=gnu99 $COMMON -I"$GLUE" -c "$GLUE/android_native_app_glue.c" -o "$OBJ/android_native_app_glue.o"

    echo "=== jeu pour $ABI"
    $CC -std=c99 $COMMON -shared -Wall -DBR_MOBILE \
        -I"$RAYLIB/src" src/*.c "$OBJ"/*.o -o "$OUT/apk/lib/$ABI/libmain.so" \
        -llog -landroid -lEGL -lGLESv2 -lOpenSLES -lm \
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
