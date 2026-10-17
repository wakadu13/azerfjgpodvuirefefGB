#!/bin/bash
# Compile la version navigateur (WebAssembly) avec Emscripten.
# Usage : bash scripts/build_web.sh <raylib compilee pour le web> [dossier de sortie]
#   La raylib web se compile avec :  make -C raylib/src PLATFORM=PLATFORM_WEB
#   (emcc doit etre dans le PATH : source emsdk/emsdk_env.sh)
# Resultat : <sortie>/index.html, un SEUL fichier qui contient tout (code,
#   WebAssembly, icone). Il marche en double-clic depuis le disque comme sur
#   un serveur web (GitHub Pages, itch.io...). Une version a plusieurs
#   fichiers (.js + .wasm) ne demarrerait pas en double-clic : le navigateur
#   refuse de lire le .wasm d'un fichier local et reste sur "Chargement...".
set -e
RAYLIB="$1"
OUT="${2:-build-web}"
mkdir -p "$OUT"

# -DBR_WEB : adaptations navigateur (boucle principale, reseau via claude.ai...)
# USE_GLFW=3 : fenetre/entrees de raylib via la GLFW d'Emscripten
# ALLOW_MEMORY_GROWTH : la memoire grandit si besoin (musique, meshes)
# STACK_SIZE : pile de 1 Mo (64 Ko par defaut, un peu juste)
# SINGLE_FILE : le WebAssembly est integre (base64) dans la page
emcc -std=gnu99 -O2 -Wall -DBR_WEB -DPLATFORM_WEB -Isrc -I"$RAYLIB/src" src/*.c "$RAYLIB/src/libraylib.a" \
    -o "$OUT/index.html" --shell-file web/shell.html \
    -s USE_GLFW=3 -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=67108864 -s STACK_SIZE=1048576 \
    -s ENVIRONMENT=web -s SINGLE_FILE=1

# L'icone (ecran de chargement, onglet) est integree elle aussi, en "data URI"
python3 - "$OUT/index.html" <<'PY'
import base64, sys
page = sys.argv[1]
icon = "data:image/png;base64," + base64.b64encode(open("assets/icon.png", "rb").read()).decode()
html = open(page, encoding="utf-8").read()
# emcc minifie la page et retire parfois les guillemets : src=icon.png
html = html.replace('"icon.png"', '"' + icon + '"').replace('=icon.png', '="' + icon + '"')
open(page, "w", encoding="utf-8").write(html)
PY
ls -la "$OUT"
