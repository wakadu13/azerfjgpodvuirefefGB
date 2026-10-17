#!/bin/bash
# Compile l'APK Android de Burger Rush, sans Gradle.
#
# Prerequis (deja installes sur les machines GitHub Actions) :
#   ANDROID_HOME  : SDK Android (build-tools + platforms)
#   ANDROID_NDK   : NDK Android
#   RAYLIB_SRC    : sources de raylib 5.5
# Resultat : $OUT/BurgerRush-android.apk (arm64-v8a pour les telephones, x86_64 pour les emulateurs)
#
# Etapes :
#   1. pour chaque architecture (ABI), compiler raylib + la colle native_app_glue du NDK,
#      puis le jeu, en une bibliotheque partagee libmain.so ;
#   2. empaqueter le manifeste, les ressources et les .so dans un APK (aapt) ;
#   3. aligner (zipalign) puis signer (apksigner) l'APK.
#
# set -e : arret a la premiere erreur ; -u : erreur si variable non definie ;
# -o pipefail : une commande qui echoue dans un pipe fait echouer tout le pipe.
set -euo pipefail

# Niveau d'API Android minimal vise (24 = Android 7.0)
API=24
# Dossier de sortie (modifiable via la variable d'environnement OUT)
OUT=${OUT:-build-android}
# ${VAR:?message} : arrete le script avec ce message si la variable est vide
NDK=${ANDROID_NDK:?ANDROID_NDK manquant}
SDK=${ANDROID_HOME:?ANDROID_HOME manquant}
RAYLIB=${RAYLIB_SRC:?RAYLIB_SRC manquant}
# Version la plus recente des build-tools et de la plateforme installees (tri par numero de version)
BT=$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)
PLATFORM_JAR=$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)/android.jar
# Compilateurs clang du NDK (hote Linux x86_64)
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
echo "build-tools : $BT"
echo "plateforme  : $PLATFORM_JAR"

# On repart d'un dossier propre ; apk/ contiendra l'arborescence lib/<abi>/libmain.so
rm -rf "$OUT"
mkdir -p "$OUT/apk"

# native_app_glue : petite couche C du NDK qui fait le lien entre l'activite Java
# NativeActivity et la boucle principale C de raylib (android_main)
GLUE="$NDK/sources/android/native_app_glue"
# Fichiers sources de raylib a compiler (un .o par module)
RAYLIB_MODULES="rcore rshapes rtextures rtext rmodels raudio utils"

# Deux architectures : arm64-v8a (vrais telephones) et x86_64 (emulateurs)
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
    # Options communes : optimisation, code relogeable (obligatoire dans un .so),
    # sections separees (permet au lieur d'eliminer le code inutile), tables de
    # deroulement (piles d'appels lisibles en cas de plantage), protection de pile
    COMMON="-O2 -fPIC -ffunction-sections -funwind-tables -fstack-protector-strong"

    # 1a. raylib : plateforme Android, rendu OpenGL ES 2
    echo "=== raylib pour $ABI"
    for m in $RAYLIB_MODULES; do
        $CC -std=gnu99 $COMMON -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2 \
            -I"$RAYLIB/src" -I"$GLUE" -c "$RAYLIB/src/$m.c" -o "$OBJ/rl_$m.o"
    done
    # 1b. la colle native_app_glue
    $CC -std=gnu99 $COMMON -I"$GLUE" -c "$GLUE/android_native_app_glue.c" -o "$OBJ/android_native_app_glue.o"

    # 1c. le jeu (BR_MOBILE active les controles tactiles), lie avec raylib en libmain.so
    # (NativeActivity charge par defaut la bibliotheque nommee "main").
    # Bibliotheques systeme : log (logcat), android, EGL + GLESv2 (affichage), OpenSLES (son), m (maths).
    # -u ANativeActivity_onCreate : force a garder ce point d'entree (sinon retire car non reference) ;
    # --no-undefined : erreur au lien si un symbole manque, plutot qu'un plantage au lancement.
    echo "=== jeu pour $ABI"
    $CC -std=c99 $COMMON -shared -Wall -DBR_MOBILE \
        -I"$RAYLIB/src" src/*.c "$OBJ"/*.o -o "$OUT/apk/lib/$ABI/libmain.so" \
        -llog -landroid -lEGL -lGLESv2 -lOpenSLES -lm \
        -u ANativeActivity_onCreate -Wl,--no-undefined
done

# 2. Creation de l'APK : aapt compile le manifeste et les ressources (icones...) en un APK sans code natif
echo "=== APK"
"$BT/aapt" package -f -M android/AndroidManifest.xml -S android/res -I "$PLATFORM_JAR" -F "$OUT/unaligned.apk"
# Ajout des bibliotheques natives ; on se place dans apk/ pour que les chemins dans l'archive soient lib/<abi>/...
(cd "$OUT/apk" && "$BT/aapt" add ../unaligned.apk lib/arm64-v8a/libmain.so lib/x86_64/libmain.so)
# 3. Alignement des donnees non compressees sur 4 octets (requis par Android, avant la signature)
"$BT/zipalign" -f -p 4 "$OUT/unaligned.apk" "$OUT/aligned.apk"
# Cle de debug commitee dans le depot : les mises a jour s'installent par-dessus l'ancienne version
"$BT/apksigner" sign --ks android/debug.keystore --ks-pass pass:android --key-pass pass:android \
    --out "$OUT/BurgerRush-android.apk" "$OUT/aligned.apk"
# Verification de la signature, puis affichage du fichier produit
"$BT/apksigner" verify "$OUT/BurgerRush-android.apk"
ls -la "$OUT/BurgerRush-android.apk"
