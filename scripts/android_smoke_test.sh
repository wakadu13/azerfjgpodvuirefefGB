#!/bin/bash
# Test de fumee sur emulateur Android : installe l'APK, lance le jeu,
# appuie sur SOLO puis sur FEU, et enregistre captures + logs.
# Usage : scripts/android_smoke_test.sh <apk> <dossier de sortie>
set -x
APK=$1
OUT=$2
mkdir -p "$OUT"

adb install -r "$APK"
adb logcat -c
adb shell am start -n io.github.wakadu13.burgerrush/android.app.NativeActivity
sleep 25
adb exec-out screencap -p > "$OUT/android-1-menu.png"

# Taille de l'ecran en paysage
SIZE=$(adb shell wm size | tail -1 | sed 's/.*: //' | tr -d '\r')
W=${SIZE%x*}; H=${SIZE#*x}
if [ "$W" -lt "$H" ]; then T=$W; W=$H; H=$T; fi
echo "ecran : ${W}x${H}"

# Bouton SOLO : centre, a 40 % de la hauteur (+ un demi-bouton de 48 unites sur 540)
adb shell input tap $((W/2)) $((H*40/100 + H*24/540))
sleep 15
adb exec-out screencap -p > "$OUT/android-2-jeu.png"

# Appui long sur FEU (centre a 115 unites du coin bas droit)
FX=$((W - 115*H/540)); FY=$((H - 115*H/540))
adb shell input swipe $FX $FY $FX $FY 3000 &
sleep 1.5
adb exec-out screencap -p > "$OUT/android-3-tir.png"
wait

adb logcat -d -s raylib > "$OUT/android-logcat.txt"
adb logcat -d | grep -iE "fatal|crash|burgerrush|libmain" | tail -60 >> "$OUT/android-logcat.txt"
adb shell pidof io.github.wakadu13.burgerrush && echo "OK : le jeu tourne toujours" >> "$OUT/android-logcat.txt"
exit 0
