#!/usr/bin/env python3
"""Store banner, icons, and the watch menu icon — the Lighthaul family's
visual language, Solfarer edition: gold route home, real spectral colors."""
import random
from PIL import Image, ImageDraw, ImageFont
import os

random.seed(20260725)
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

GOLD = (255, 170, 0)
WHITE = (255, 255, 255)
SPEC = [(255, 110, 80)] * 5 + [(255, 170, 80)] * 5 + [(255, 220, 120)] * 3 + \
       [(250, 245, 210)] * 2 + [(235, 240, 255)] * 1 + [(200, 220, 255)] * 1
# weighted like the real bubble: mostly M/K red-orange, some G/F, few A/D


def futura(size, index=0):
    return ImageFont.truetype("/System/Library/Fonts/Supplemental/Futura.ttc",
                              size, index=index)


def starfield(d, w, h, n, exclude=None):
    for _ in range(n):
        x, y = random.randint(0, w - 1), random.randint(0, h - 1)
        if exclude and any(x0 <= x <= x1 and y0 <= y <= y1
                           for x0, y0, x1, y1 in exclude):
            continue
        c = random.choice(SPEC)
        r = 1 if random.random() < 0.12 else 0
        d.ellipse([x - r, y - r, x + r, y + r], fill=c)


# ---------------------------------------------------------------- banner
W, H = 720, 320
img = Image.new("RGB", (W, H), (0, 0, 0))
d = ImageDraw.Draw(img)

shot = Image.open(os.path.join(ROOT, "store/screenshots/emery_1_map.png")).convert("RGB")
bs = shot.resize((int(200 * 1.15), int(228 * 1.15)), Image.NEAREST)   # 230x262
bx, by = W - bs.width - 26, (H - bs.height) // 2

starfield(d, W, H, 170, exclude=[(bx - 4, by - 4, bx + bs.width + 4, by + bs.height + 4)])

# the route home: Sol ringed gold, the wander reaching out to a red dwarf
x0, y0 = 62, 250
x1, y1 = 452, 60
d.line([x0, y0, x1, y1], fill=GOLD, width=4)
d.ellipse([x0 - 13, y0 - 13, x0 + 13, y0 + 13], outline=GOLD, width=4)
d.ellipse([x0 - 4, y0 - 4, x0 + 4, y0 + 4], fill=GOLD)
d.ellipse([x1 - 8, y1 - 8, x1 + 8, y1 + 8], fill=(255, 110, 80))     # M dwarf
t = 0.55
sx, sy = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
d.ellipse([sx - 6, sy - 6, sx + 6, sy + 6], fill=WHITE)

d.rectangle([bx - 3, by - 3, bx + bs.width + 2, by + bs.height + 2],
            outline=(60, 60, 60), width=3)
img.paste(bs, (bx, by))

d = ImageDraw.Draw(img)
d.text((64, 46), "SOLFARER", font=futura(66, index=2), fill=GOLD)
d.text((67, 126), "WANDER THE REAL STARS", font=futura(26, index=0),
       fill=(0, 230, 230))
d.text((78, 258), "a new journey from Sol, every day",
       font=futura(24, index=1), fill=WHITE)

img.save(os.path.join(ROOT, "store/banner_720x320.png"))


# ---------------------------------------------------------------- icons
def icon(size):
    im = Image.new("RGB", (size, size), (0, 0, 0))
    dd = ImageDraw.Draw(im)
    s = size / 48.0
    random.seed(11)
    for _ in range(14):
        x, y = random.randint(2, size - 3), random.randint(2, size - 3)
        r = max(1, int(0.6 * s))
        dd.ellipse([x - r // 2, y - r // 2, x + r // 2, y + r // 2],
                   fill=random.choice(SPEC))
    x0, y0 = 11 * s, 37 * s
    x1, y1 = 38 * s, 11 * s
    dd.line([x0, y0, x1, y1], fill=GOLD, width=max(2, int(2.4 * s)))
    rr = 4.6 * s
    dd.ellipse([x0 - rr, y0 - rr, x0 + rr, y0 + rr], outline=GOLD,
               width=max(2, int(1.8 * s)))
    dd.ellipse([x0 - 1.4 * s, y0 - 1.4 * s, x0 + 1.4 * s, y0 + 1.4 * s], fill=GOLD)
    r2 = 3.6 * s
    dd.ellipse([x1 - r2, y1 - r2, x1 + r2, y1 + r2], fill=(255, 110, 80))
    tt = 0.55
    sx, sy = x0 + (x1 - x0) * tt, y0 + (y1 - y0) * tt
    r3 = 2.0 * s
    dd.ellipse([sx - r3, sy - r3, sx + r3, sy + r3], fill=WHITE)
    return im


icon(48).save(os.path.join(ROOT, "store/icon_small_48.png"))
icon(144).save(os.path.join(ROOT, "store/icon_large_144.png"))
icon(25).save(os.path.join(ROOT, "resources/images/menu_icon.png"))
print("banner, icons, and menu icon written")
