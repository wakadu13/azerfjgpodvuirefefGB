#!/bin/bash
# Test de fumee sur emulateur Android : installe l'APK, lance le jeu,
# appuie sur SOLO puis sur FEU, et enregistre captures + logs.
# Usage : scripts/android_smoke_test.sh <apk> <dossier de sortie>
# set -x : affiche chaque commande avant de l'executer (pratique dans les logs de CI).
# Pas de "set -e" : on veut aller jusqu'au bout pour recuperer captures et logs.
set -x
# Arguments : chemin de l'APK et dossier ou ranger les resultats
APK=$1
OUT=$2
mkdir -p "$OUT"

# root : permet de recuperer la pile des threads du jeu en cas de blocage
adb root || true
# Attend que l'emulateur soit pret, installe l'APK (-r : remplace une version existante)
# et vide les logs pour ne garder que ceux de ce test
adb wait-for-device
adb install -r "$APK"
adb logcat -c
# Lance l'activite native du jeu, puis laisse 25 s pour le demarrage (emulateur lent)
adb shell am start -n io.github.wakadu13.zeffaliecomeback/android.app.NativeActivity
sleep 25
# Capture d'ecran du menu (exec-out : sortie binaire brute, sans conversion des fins de ligne)
adb exec-out screencap -p > "$OUT/android-1-menu.png"
# PID du processus du jeu (tr enleve le \r ajoute par adb shell)
PID=$(adb shell pidof io.github.wakadu13.zeffaliecomeback | tr -d '\r')
# Pile de tous les threads du jeu, utile si le jeu est bloque
adb shell debuggerd -b "$PID" > "$OUT/android-threads.txt" 2>&1 || true

# Taille de l'ecran en paysage
# "wm size" renvoie par ex. "Physical size: 1080x2400" : on garde la partie apres ": "
SIZE=$(adb shell wm size | tail -1 | sed 's/.*: //' | tr -d '\r')
# Decoupe "LxH" en largeur W et hauteur H (expansions de chaines bash)
W=${SIZE%x*}; H=${SIZE#*x}
# Le jeu est en paysage : on echange W et H si l'ecran est decrit en portrait
if [ "$W" -lt "$H" ]; then T=$W; W=$H; H=$T; fi
echo "ecran : ${W}x${H}"

# Les coordonnees sont converties des unites virtuelles du jeu (540 de haut sur mobile, voir ui.c) en pixels
# Bouton SOLO : centre, a 40 % de la hauteur (+ un demi-bouton de 48 unites sur 540)
adb shell input tap $((W/2)) $((H*40/100 + H*24/540))
sleep 15
adb exec-out screencap -p > "$OUT/android-2-jeu.png"

# Appui long sur FEU (centre a 115 unites du coin bas droit)
# Position en pixels : 115 unites virtuelles * (H / 540)
FX=$((W - 115*H/540)); FY=$((H - 115*H/540))
# swipe sur place pendant 3000 ms = appui long ; lance en arriere-plan (&) pour capturer pendant le tir
adb shell input swipe $FX $FY $FX $FY 3000 &
sleep 1.5
adb exec-out screencap -p > "$OUT/android-3-tir.png"
# Attend la fin de l'appui long
wait

# Logs de raylib, puis lignes interessantes des logs systeme (plantages, ANR = application bloquee)
adb logcat -d -s raylib > "$OUT/android-logcat.txt"
adb logcat -d | grep -iE "fatal|crash|zeffaliecomeback|libmain|ANR" | tail -80 >> "$OUT/android-logcat.txt"
# Plantages natifs (pile d'appels) : tampon "crash" du logcat
adb logcat -d -b crash > "$OUT/android-crash.txt" 2>&1 || true
# Recopies dans le journal du job : lisibles directement depuis GitHub en cas d'echec
echo "----- logcat (raylib + systeme) -----"; tail -60 "$OUT/android-logcat.txt"
echo "----- logcat (plantages) -----"; tail -60 "$OUT/android-crash.txt"

# Echec si le jeu ne tourne plus ou si l'affichage n'a pas pu s'initialiser
if ! adb shell pidof io.github.wakadu13.zeffaliecomeback; then echo "ECHEC : le jeu s'est arrete"; exit 1; fi
if grep -qE "Failed to create EGL|Failed to (compile|link)" "$OUT/android-logcat.txt"; then echo "ECHEC : erreur graphique"; exit 1; fi
if grep -q "ANR in io.github.wakadu13.zeffaliecomeback" "$OUT/android-logcat.txt"; then echo "ECHEC : le jeu ne repond plus (ANR)"; exit 1; fi
# raylib ecrit "Program shader loaded" quand un shader est compile et lie avec succes
grep -q "Program shader loaded" "$OUT/android-logcat.txt" || { echo "ECHEC : shaders non charges"; exit 1; }
echo "OK"
exit 0
