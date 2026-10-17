#!/usr/bin/env python3
# Transforme assets/kyrie_des_gueux.ogg en tableau C (src/kyrie_ogg.c) pour que
# la musique soit integree a l'executable : le jeu reste un seul fichier sur
# PC, et marche tel quel sur Android et iOS (pas de gestion de fichiers).
# A relancer si on change la musique :  python3 scripts/embed_music.py
import os
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
data = open(os.path.join(root, "assets", "kyrie_des_gueux.ogg"), "rb").read()
with open(os.path.join(root, "src", "kyrie_ogg.c"), "w") as out:
    out.write("/*\n * kyrie_ogg.c - FICHIER GENERE par scripts/embed_music.py, ne pas modifier.\n"
              " * Contenu de assets/kyrie_des_gueux.ogg (musique de fond, OGG Vorbis).\n */\n")
    out.write("const unsigned int KYRIE_OGG_SIZE = %d;\n" % len(data))
    out.write("const unsigned char KYRIE_OGG[%d] = {\n" % len(data))
    for i in range(0, len(data), 32):
        out.write(",".join(str(b) for b in data[i:i + 32]) + ",\n")
    out.write("};\n")
