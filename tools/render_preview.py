#!/usr/bin/env python3
"""Preview renders: the whole Local Bubble, and a watch-style viewport mock."""
import csv, math, os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
LY = 3.26156
R = [[-0.0548755604, -0.8734370902, -0.4838350155],
     [ 0.4941094279, -0.4448296300,  0.7469822445],
     [-0.8676661490, -0.1980763734,  0.4559837762]]

SPEC_COL = {'O': (150, 180, 255), 'B': (170, 195, 255), 'A': (235, 240, 255),
            'F': (250, 245, 210), 'G': (255, 220, 120), 'K': (255, 170, 80),
            'M': (255, 110, 80),  'D': (200, 220, 255)}
GRAYISH = (150, 150, 150)

stars = []
with open(os.path.join(HERE, 'hygdata_v41.csv')) as f:
    for r in csv.DictReader(f):
        if r['id'] == '0':
            continue
        try:
            d = float(r['dist'])
        except ValueError:
            continue
        if d <= 0 or d >= 90000 or d * LY > 100:
            continue
        xe, ye, ze = float(r['x']), float(r['y']), float(r['z'])
        x = (R[0][0]*xe + R[0][1]*ye + R[0][2]*ze) * LY
        y = (R[1][0]*xe + R[1][1]*ye + R[1][2]*ze) * LY
        z = (R[2][0]*xe + R[2][1]*ye + R[2][2]*ze) * LY
        am = float(r['absmag']) if r['absmag'] else 10
        if d * LY > 50 and am > 8.0 and not (r['proper'] or r['bf']):
            continue
        stars.append((x, y, z, (r['spect'].strip() or '?')[0].upper(),
                      am, r['proper']))

print(f"{len(stars)} stars in render set")

# ---- the whole bubble, top-down galactic plane
W = 720
img = Image.new('RGB', (W, W), (0, 0, 0))
d = ImageDraw.Draw(img)
SC = W / 210.0                     # 100ly radius + margin
def px(x): return W // 2 + int(x * SC)
def py(y): return W // 2 - int(y * SC)
for rr in (25, 50, 75, 100):
    d.ellipse([px(-rr), py(rr), px(rr), py(-rr)], outline=(38, 38, 38))
for (x, y, z, sp, am, name) in stars:
    c = SPEC_COL.get(sp, GRAYISH)
    r_ = 2 if am < 2 else (1 if am < 7 else 0)
    d.ellipse([px(x)-r_, py(y)-r_, px(x)+r_, py(y)+r_], fill=c)
d.ellipse([px(0)-5, py(0)-5, px(0)+5, py(0)+5], outline=(255, 170, 0), width=2)
try:
    f = ImageFont.truetype('/System/Library/Fonts/Supplemental/Futura.ttc', 16)
except OSError:
    f = None
d.text((10, 8), '100 ly Local Bubble - 2,234 systems, top-down galactic plane',
       fill=(160, 160, 160), font=f)
d.text((px(0)+8, py(0)-4), 'SOL', fill=(255, 170, 0), font=f)
img.save(os.path.join(HERE, 'bubble.png'))

# ---- watch-style mock: 40-ly viewport at Sol, z-slab +/-12 ly, off-center
MW, MH = 144, 168
m = Image.new('RGB', (MW, MH), (0, 0, 0))
md = ImageDraw.Draw(m)
top_h, bot_h = 50, 36
cx, cy = -6.0, -4.0            # viewport center offset: Sol rides off-center
VIEW = 40.0
msc = (MW - 8) / VIEW
def mx(x): return MW // 2 + int((x - cx) * msc)
def my(y): return top_h + (MH - top_h - bot_h) // 2 - int((y - cy) * msc)
vis = [(x, y, z, sp, am, nm) for (x, y, z, sp, am, nm) in stars
       if abs(z) < 12 and abs(x - cx) < VIEW * 0.75 and abs(y - cy) < VIEW * 0.75]
print(f"{len(vis)} stars in the mock viewport")
# route to Proxima-ish (nearest)
vis_sorted = sorted(vis, key=lambda s: s[0]*s[0] + s[1]*s[1])
tx, ty = vis_sorted[0][0], vis_sorted[0][1]
md.line([mx(0), my(0), mx(tx), my(ty)], fill=(255, 170, 0), width=2)
for (x, y, z, sp, am, nm) in vis:
    c = SPEC_COL.get(sp, GRAYISH)
    r_ = 2 if am < 2 else (1 if am < 8 else 0)
    md.ellipse([mx(x)-r_, my(y)-r_, mx(x)+r_, my(y)+r_], fill=c)
md.ellipse([mx(0)-3, my(0)-3, mx(0)+3, my(0)+3], fill=(255, 170, 0))
md.ellipse([mx(0)-6, my(0)-6, mx(0)+6, my(0)+6], outline=(255, 170, 0))
md.ellipse([mx(tx)-3, my(ty)-3, mx(tx)+3, my(ty)+3], outline=(0, 230, 230))
md.text((mx(tx)+5, my(ty)-12), vis_sorted[0][5] or 'Proxima', fill=(0, 230, 230))
# chrome placeholders
md.text((2, 0), '10:37', fill=(255, 255, 255))
md.text((100, 0), 'FRI 24', fill=(160, 160, 160))
md.text((26, 32), '@Sol   0.0 ly out', fill=(160, 160, 160))
md.line([0, top_h - 2, MW, top_h - 2], fill=(60, 60, 60))
md.line([0, MH - bot_h, MW, MH - bot_h], fill=(60, 60, 60))
md.text((4, MH - 34), '> Proxima Centauri', fill=(255, 255, 255))
md.text((4, MH - 18), '4.2 ly   M5   4.2 yr away', fill=(0, 200, 100))
m = m.resize((MW * 2, MH * 2), Image.NEAREST)
m.save(os.path.join(HERE, 'viewport_mock.png'))
print('wrote bubble.png and viewport_mock.png')
