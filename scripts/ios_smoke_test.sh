#!/bin/bash
# Test de fumee dans le simulateur iOS : installe l'app, capture le menu,
# puis relance en partie directe (BR_AUTOSTART) et capture le jeu.
# Usage : scripts/ios_smoke_test.sh <BurgerRush.app> <dossier de sortie>
# set -x : affiche chaque commande executee (logs de CI)
set -x
# Arguments : l'application .app et le dossier de sortie ; BUNDLE = identifiant de l'app
APP=$1
OUT=$2
BUNDLE=io.github.wakadu13.burgerrush
mkdir -p "$OUT"

# Choisit le premier simulateur iPhone disponible : simctl liste les appareils en JSON,
# et un petit script Python extrait l'UDID (identifiant unique) du premier iPhone iOS
UDID=$(xcrun simctl list devices available -j | python3 -c '
import json, sys
devices = json.load(sys.stdin)["devices"]
print(next(d["udid"] for runtime, lst in devices.items() if "iOS" in runtime for d in lst if d["name"].startswith("iPhone")))')
echo "simulateur : $UDID"
# Demarre le simulateur, attend qu'il soit completement pret (-b), puis installe l'app
xcrun simctl boot "$UDID"
xcrun simctl bootstatus "$UDID" -b
xcrun simctl install "$UDID" "$APP"

# 1er lancement : menu. Sorties standard et erreur redirigees vers des fichiers (chemins absolus requis)
xcrun simctl launch --stdout="$PWD/$OUT/ios-stdout-menu.txt" --stderr="$PWD/$OUT/ios-stderr-menu.txt" "$UDID" $BUNDLE
sleep 25
xcrun simctl io "$UDID" screenshot "$OUT/ios-1-menu.png"
xcrun simctl terminate "$UDID" $BUNDLE

# 2e lancement : les variables prefixees SIMCTL_CHILD_ sont transmises a l'app (sans le prefixe) ;
# BR_AUTOSTART=1 fait demarrer directement une partie
SIMCTL_CHILD_BR_AUTOSTART=1 xcrun simctl launch --stdout="$PWD/$OUT/ios-stdout-jeu.txt" --stderr="$PWD/$OUT/ios-stderr-jeu.txt" "$UDID" $BUNDLE
sleep 10
xcrun simctl io "$UDID" screenshot "$OUT/ios-2-jeu.png"

# Echec si l'affichage n'a pas pu s'initialiser (nos shaders = 2e programme charge)
if grep -qE "Failed to (compile|link|create)" "$OUT"/ios-stdout-*.txt; then echo "ECHEC : erreur graphique"; exit 1; fi
# raylib charge d'abord son shader par defaut : au moins 2 "Program shader loaded" = notre shader aussi
[ "$(grep -c "Program shader loaded" "$OUT/ios-stdout-jeu.txt")" -ge 2 ] || { echo "ECHEC : shaders du jeu non charges"; exit 1; }
echo "OK"
exit 0
