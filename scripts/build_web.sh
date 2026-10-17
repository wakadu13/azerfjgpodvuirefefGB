#!/bin/bash
# Compile la version navigateur (WebAssembly) avec Emscripten.
# Usage : bash scripts/build_web.sh <raylib compilee pour le web> [dossier de sortie]
#   La raylib web se compile avec :  make -C raylib/src PLATFORM=PLATFORM_WEB
#   (emcc doit etre dans le PATH : source emsdk/emsdk_env.sh)
# Resultat : <sortie>/index.html, index.js, index.wasm, icon.png
#   -> a servir par n'importe quel serveur web (GitHub Pages, etc.).
#   Ouvrir index.html directement depuis le disque ne marche pas : il faut un
#   serveur, par exemple  python3 -m http.server -d build-web
set -e
RAYLIB="$1"
OUT="${2:-build-web}"
mkdir -p "$OUT"

# -DBR_WEB : adaptations navigateur (boucle principale, pas de reseau...)
# USE_GLFW=3 : fenetre/entrees de raylib via la GLFW d'Emscripten
# ALLOW_MEMORY_GROWTH : la memoire grandit si besoin (musique, meshes)
# STACK_SIZE : pile de 1 Mo (64 Ko par defaut, un peu juste)
emcc -std=gnu99 -O2 -Wall -DBR_WEB -DPLATFORM_WEB -Isrc -I"$RAYLIB/src" src/*.c "$RAYLIB/src/libraylib.a" \
    -o "$OUT/index.html" --shell-file web/shell.html \
    -s USE_GLFW=3 -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=67108864 -s STACK_SIZE=1048576 \
    -s ENVIRONMENT=web
cp assets/icon.png "$OUT/icon.png"
ls -la "$OUT"
