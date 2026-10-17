#!/usr/bin/env python3
# Corrige raylib 5.5 pour Android avant de le compiler (appele par build_android.sh).
# Usage : python3 scripts/patch_raylib_android.py <sources raylib>/src/platforms/rcore_android.c
#
# Probleme : le jeu est en paysage, mais un telephone (ou l'emulateur des tests)
# est souvent en portrait quand on le lance. Android tourne alors l'ecran juste
# apres avoir cree la fenetre du jeu, et raylib 5.5 ne gere pas ce changement de
# taille : il garde la surface de dessin du debut (parfois 1 x 1 pixel, d'ou un
# ecran noir) avec un cadrage faux.
#
# Correctif : quand la fenetre change de taille (APP_CMD_WINDOW_RESIZED,
# APP_CMD_CONTENT_RECT_CHANGED, APP_CMD_CONFIG_CHANGED), on recree la surface
# EGL a la nouvelle taille et on met a jour les tailles d'ecran de raylib,
# comme si la fenetre avait ete creee directement a la bonne taille.
# Les ressources (textures, shaders, meshes) sont conservees : seul le
# contexte OpenGL est detache puis rattache, il n'est jamais detruit.
#
# Le script ne fait rien si le correctif est deja applique.
import sys

MARK = "[Zeffalie Come Back]"

OLD = """        case APP_CMD_CONFIG_CHANGED:
        {
            //AConfiguration_fromAssetManager(platform.app->config, platform.app->activity->assetManager);
            //print_cur_config(platform.app);

            // Check screen orientation here!
        } break;"""

NEW = """        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONTENT_RECT_CHANGED:
        case APP_CMD_CONFIG_CHANGED:
        {
            // [Zeffalie Come Back] La fenetre a change de taille (rotation de l'ecran
            // au demarrage...) : on recree la surface de dessin a la nouvelle taille
            if ((app->window != NULL) && CORE.Window.ready && (platform.device != EGL_NO_DISPLAY) &&
                (platform.surface != EGL_NO_SURFACE))
            {
                int width = ANativeWindow_getWidth(app->window);
                int height = ANativeWindow_getHeight(app->window);

                if ((width > 1) && (height > 1) &&
                    ((width != CORE.Window.display.width) || (height != CORE.Window.display.height)))
                {
                    TRACELOG(LOG_INFO, "ANDROID: Window resized to %i x %i", width, height);

                    eglMakeCurrent(platform.device, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                    eglDestroySurface(platform.device, platform.surface);

                    CORE.Window.display.width = width;
                    CORE.Window.display.height = height;
                    CORE.Window.screen.width = width;
                    CORE.Window.screen.height = height;
                    CORE.Window.render.width = width;
                    CORE.Window.render.height = height;
                    CORE.Window.currentFbo.width = width;
                    CORE.Window.currentFbo.height = height;
                    CORE.Window.renderOffset.x = 0;
                    CORE.Window.renderOffset.y = 0;
                    CORE.Window.screenScale = MatrixIdentity();

                    EGLint displayFormat = 0;
                    eglGetConfigAttrib(platform.device, platform.config, EGL_NATIVE_VISUAL_ID, &displayFormat);
                    ANativeWindow_setBuffersGeometry(app->window, width, height, displayFormat);

                    platform.surface = eglCreateWindowSurface(platform.device, platform.config, app->window, NULL);
                    eglMakeCurrent(platform.device, platform.surface, platform.surface, platform.context);

                    SetupViewport(width, height);
                    CORE.Window.resizedLastFrame = true;
                }
            }
        } break;"""


def main():
    path = sys.argv[1]
    with open(path, encoding="utf-8") as f:
        src = f.read()
    if MARK in src:
        print("raylib Android : correctif deja applique")
        return
    if OLD not in src:
        sys.exit("raylib Android : bloc APP_CMD_CONFIG_CHANGED introuvable (version de raylib differente ?)")
    with open(path, "w", encoding="utf-8") as f:
        f.write(src.replace(OLD, NEW))
    print("raylib Android : correctif de rotation applique")


if __name__ == "__main__":
    main()
