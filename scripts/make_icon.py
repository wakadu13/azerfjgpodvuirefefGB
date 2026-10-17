#!/usr/bin/env python3
# Dessine l'icone du jeu (un poussin enchaine, de nuit) et l'exporte a toutes
# les tailles utiles :
#   assets/icon.png                     1024 px (reference)
#   assets/icon.ico                     Windows (icone de l'exe et de la fenetre)
#   assets/icon.icns                    macOS (ZeffalieComeBack.app)
#   android/res/drawable/icon.png       192 px
#   ios/icons/AppIcon60x60@2x.png       120 px
#   ios/icons/AppIcon60x60@3x.png       180 px
#   src/icon_data.c                     64 px en pixels bruts : icone de la fenetre (Linux, Windows, Mac)
# A relancer si on modifie le dessin :  python3 scripts/make_icon.py  (necessite Pillow)
import math
import os

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = 2048                    # on dessine en 2048 px puis on reduit : bords lisses (anticrenelage)


def P(v):
    """Coordonnee d'un dessin pense en 1024 px -> echelle de travail."""
    return int(v * S / 1024)


def ellipse(d, cx, cy, rx, ry, **kw):
    d.ellipse([P(cx - rx), P(cy - ry), P(cx + rx), P(cy + ry)], **kw)


def link(img, cx, cy, angle, flat):
    """Un maillon de chaine centre en (cx, cy), oriente selon angle (degres).
    flat = maillon vu a plat (anneau ouvert) ; sinon vu de profil (barre)."""
    w, h = (150, 92) if flat else (150, 40)
    layer = Image.new("RGBA", (P(w + 40), P(h + 40)), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    box = [P(20), P(20), P(20 + w), P(20 + h)]
    d.rounded_rectangle(box, radius=P(h / 2), outline=(40, 44, 52, 255), width=P(30))      # contour sombre
    d.rounded_rectangle(box, radius=P(h / 2), outline=(176, 182, 192, 255), width=P(20))   # acier
    d.arc([box[0] + P(6), box[1] + P(4), box[2] - P(6), box[3] - P(4)], 200, 330,
          fill=(236, 240, 246, 255), width=P(7))                                           # reflet
    layer = layer.rotate(angle, resample=Image.BICUBIC, expand=True)
    img.alpha_composite(layer, (P(cx) - layer.width // 2, P(cy) - layer.height // 2))


def draw():
    img = Image.new("RGBA", (S, S))
    d = ImageDraw.Draw(img)

    # Fond : nuit (degrade vertical), lune et etoiles, comme dans le jeu
    for y in range(S):
        t = y / S
        d.line([(0, y), (S, y)], fill=(int(8 + 26 * t), int(12 + 30 * t), int(32 + 44 * t), 255))
    ellipse(d, 800, 190, 92, 92, fill=(70, 84, 130, 255))
    ellipse(d, 800, 190, 70, 70, fill=(236, 238, 224, 255))
    ellipse(d, 822, 172, 16, 16, fill=(206, 208, 194, 255))
    ellipse(d, 782, 214, 11, 11, fill=(212, 214, 200, 255))
    for (x, y, r) in [(140, 130, 6), (300, 80, 4), (470, 160, 5), (620, 70, 4), (930, 330, 5), (90, 330, 4), (640, 250, 3)]:
        ellipse(d, x, y, r, r, fill=(230, 236, 255, 255))

    # Ombre au sol
    ellipse(d, 512, 900, 300, 46, fill=(14, 18, 40, 255))

    # Poussin : pattes, corps, aile, tete, houppette, bec, oeil
    orange = (255, 136, 28, 255)
    for x in (430, 590):
        d.rounded_rectangle([P(x - 14), P(800), P(x + 14), P(880)], radius=P(10), fill=orange)
        d.rounded_rectangle([P(x - 50), P(862), P(x + 50), P(890)], radius=P(14), fill=orange)
    ellipse(d, 500, 640, 270, 245, fill=(232, 178, 24, 255))      # corps (bord ombre)
    ellipse(d, 490, 625, 255, 228, fill=(255, 214, 50, 255))      # corps
    ellipse(d, 450, 560, 120, 80, fill=(255, 232, 120, 255))      # reflet
    ellipse(d, 640, 380, 158, 152, fill=(232, 178, 24, 255))      # tete (bord ombre)
    ellipse(d, 632, 370, 150, 144, fill=(255, 214, 50, 255))      # tete
    for (x, y, rx, ry) in [(600, 222, 22, 50), (640, 214, 18, 46)]:   # houppette
        ellipse(d, x, y, rx, ry, fill=(255, 214, 50, 255))
    d.polygon([(P(760), P(350)), (P(880), P(395)), (P(760), P(440))], fill=orange)     # bec
    d.line([(P(764), P(395)), (P(860), P(395))], fill=(200, 92, 10, 255), width=P(8))
    ellipse(d, 690, 330, 34, 40, fill=(20, 16, 14, 255))          # oeil
    ellipse(d, 700, 316, 12, 13, fill=(255, 255, 255, 255))
    d.line([(P(640), P(262)), (P(728), P(286))], fill=(120, 80, 10, 255), width=P(14))   # sourcil inquiet
    ellipse(d, 300, 640, 70, 120, fill=(236, 186, 32, 255))       # aile

    # Chaine : maillons en diagonale sur le corps, a plat et de profil en alternance
    x0, y0, x1, y1 = 180, 470, 860, 830
    n = 9
    ang = -math.degrees(math.atan2(y1 - y0, x1 - x0))
    for i in range(n):
        t = i / (n - 1)
        link(img, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, ang, i % 2 == 0)

    # Cadenas accroche a la chaine
    d = ImageDraw.Draw(img)
    cx, cy = 560, 760
    d.arc([P(cx - 52), P(cy - 110), P(cx + 52), P(cy - 6)], 180, 360, fill=(40, 44, 52, 255), width=P(30))
    d.arc([P(cx - 52), P(cy - 110), P(cx + 52), P(cy - 6)], 180, 360, fill=(176, 182, 192, 255), width=P(18))
    d.rounded_rectangle([P(cx - 80), P(cy - 60), P(cx + 80), P(cy + 70)], radius=P(22),
                        fill=(214, 168, 46, 255), outline=(110, 80, 20, 255), width=P(10))
    ellipse(d, cx, cy - 2, 16, 16, fill=(70, 50, 14, 255))        # trou de serrure
    d.polygon([(P(cx - 9), P(cy + 4)), (P(cx + 9), P(cy + 4)), (P(cx + 5), P(cy + 40)), (P(cx - 5), P(cy + 40))],
              fill=(70, 50, 14, 255))
    return img.filter(ImageFilter.SMOOTH)


def main():
    big = draw().resize((1024, 1024), Image.LANCZOS)
    os.makedirs(os.path.join(ROOT, "assets"), exist_ok=True)
    big.save(os.path.join(ROOT, "assets", "icon.png"))
    big.save(os.path.join(ROOT, "assets", "icon.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    big.save(os.path.join(ROOT, "assets", "icon.icns"))
    rgb = big.convert("RGB")   # iOS : icone opaque (pas de transparence)
    big.resize((192, 192), Image.LANCZOS).save(os.path.join(ROOT, "android", "res", "drawable", "icon.png"))
    rgb.resize((120, 120), Image.LANCZOS).save(os.path.join(ROOT, "ios", "icons", "AppIcon60x60@2x.png"))
    rgb.resize((180, 180), Image.LANCZOS).save(os.path.join(ROOT, "ios", "icons", "AppIcon60x60@3x.png"))
    # Icone de la fenetre : pixels RGBA bruts dans un tableau C (aucun fichier a charger)
    small = big.resize((64, 64), Image.LANCZOS)
    raw = small.tobytes()
    with open(os.path.join(ROOT, "src", "icon_data.c"), "w") as out:
        out.write("/*\n * icon_data.c - FICHIER GENERE par scripts/make_icon.py, ne pas modifier.\n"
                  " * Icone de la fenetre : 64 x 64 pixels RGBA (poussin enchaine).\n */\n")
        out.write("const int ICON_SIZE = 64;\n")
        out.write("const unsigned char ICON_RGBA[%d] = {\n" % len(raw))
        for i in range(0, len(raw), 32):
            out.write(",".join(str(b) for b in raw[i:i + 32]) + ",\n")
        out.write("};\n")
    print("icones generees")


if __name__ == "__main__":
    main()
