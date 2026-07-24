#!/usr/bin/env python3
"""Curate the HYG catalog into a Pebble-sized Local Bubble.

Reads hygdata_v41.csv, converts to galactic XYZ, dedupes multi-star systems,
resolves display names, then reports: counts by radius, name coverage,
packed-size estimates, and walk connectivity at candidate hop radii.
"""
import csv, math, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
LY_PER_PC = 3.26156

# J2000 equatorial -> galactic rotation
R = [[-0.0548755604, -0.8734370902, -0.4838350155],
     [ 0.4941094279, -0.4448296300,  0.7469822445],
     [-0.8676661490, -0.1980763734,  0.4559837762]]

GREEK = {  # bayer 'bf' uses 3-letter abbreviations
 'Alp':'Alpha','Bet':'Beta','Gam':'Gamma','Del':'Delta','Eps':'Epsilon',
 'Zet':'Zeta','Eta':'Eta','The':'Theta','Iot':'Iota','Kap':'Kappa',
 'Lam':'Lambda','Mu':'Mu','Nu':'Nu','Xi':'Xi','Omi':'Omicron','Pi':'Pi',
 'Rho':'Rho','Sig':'Sigma','Tau':'Tau','Ups':'Upsilon','Phi':'Phi',
 'Chi':'Chi','Psi':'Psi','Ome':'Omega'}

def gal(x, y, z):
    return (R[0][0]*x + R[0][1]*y + R[0][2]*z,
            R[1][0]*x + R[1][1]*y + R[1][2]*z,
            R[2][0]*x + R[2][1]*y + R[2][2]*z)

def pick_name(r):
    if r['proper']:
        return r['proper'], 3
    bf = r['bf'].strip()
    if bf:
        parts = bf.split()
        if len(parts) >= 2:
            g = GREEK.get(parts[0].rstrip('-0123456789'))
            if g:
                return f"{g} {parts[-1]}", 2
        return bf, 2
    if r['gl']:
        gl = r['gl'].strip()
        return (gl if gl.startswith(('GJ','Gl','NN','Wo')) else 'GJ ' + gl), 1
    if r['hip']:
        return 'HIP ' + r['hip'], 0
    if r['hd']:
        return 'HD ' + r['hd'], 0
    return 'Star ' + r['id'], 0

MAX_LY = 130.0
stars = []
with open(os.path.join(HERE, 'hygdata_v41.csv')) as f:
    for r in csv.DictReader(f):
        if r['id'] == '0':
            continue                       # Sol handled specially at origin
        try:
            dpc = float(r['dist'])
        except ValueError:
            continue
        if dpc <= 0 or dpc >= 90000:      # unknown distance sentinel
            continue
        dly = dpc * LY_PER_PC
        if dly > MAX_LY:
            continue
        x, y, z = gal(float(r['x']), float(r['y']), float(r['z']))
        name, q = pick_name(r)
        stars.append({
            'name': name, 'q': q,
            'x': x * LY_PER_PC, 'y': y * LY_PER_PC, 'z': z * LY_PER_PC,
            'dly': dly,
            'mag': float(r['mag']) if r['mag'] else 15.0,
            'absmag': float(r['absmag']) if r['absmag'] else 10.0,
            'spect': (r['spect'].strip() or '?')[0].upper(),
            'con': r['con'].strip(),
            'base': r['base'].strip() or r['id'],
        })

# --- dedupe multi-star systems: keep the brightest component per base id,
# --- and also merge anything closer than 0.1 ly (unlinked doubles)
by_base = {}
for s in stars:
    k = s['base']
    if k not in by_base or s['mag'] < by_base[k]['mag']:
        by_base[k] = s
sys_stars = sorted(by_base.values(), key=lambda s: s['dly'])
merged = []
for s in sys_stars:
    dup = False
    for t in merged:
        if abs(s['x']-t['x']) < 0.15 and abs(s['y']-t['y']) < 0.15 and abs(s['z']-t['z']) < 0.15:
            dup = True
            break
    if not dup:
        merged.append(s)

def count_within(ly):
    return sum(1 for s in merged if s['dly'] <= ly)

print(f"raw rows within {MAX_LY:.0f} ly: {len(stars)}, systems after dedupe: {len(merged)}")
for r_ in (25, 50, 80, 100, 125):
    print(f"  systems within {r_:>3} ly: {count_within(r_)}")

named = [s for s in merged if s['q'] >= 2]
print(f"proper/bayer-named systems: {len(named)} "
      f"(proper only: {sum(1 for s in merged if s['q']==3)})")

# --- candidate catalog: everything within 100 ly, thinned beyond by brightness
def build(radius, dim_absmag_cap, target_note):
    cat = [s for s in merged if s['dly'] <= radius and
           (s['dly'] <= 50 or s['absmag'] <= dim_absmag_cap or s['q'] >= 2)]
    names_blob = sum(len(s['name']) + 1 for s in cat)
    packed = len(cat) * 12 + names_blob
    print(f"\n[{target_note}] radius {radius} ly, dim cap absmag {dim_absmag_cap}: "
          f"{len(cat)} stars, ~{packed/1024:.1f} KB packed")
    # connectivity from Sol
    for hop in (15, 20, 25):
        seen = set()
        frontier = [(0.0, 0.0, 0.0)]
        pts = [(s['x'], s['y'], s['z']) for s in cat]
        idx_seen = set()
        while frontier:
            cx, cy, cz = frontier.pop()
            for i, (px, py, pz) in enumerate(pts):
                if i in idx_seen:
                    continue
                if (px-cx)**2 + (py-cy)**2 + (pz-cz)**2 <= hop*hop:
                    idx_seen.add(i)
                    frontier.append((px, py, pz))
        print(f"    hop {hop} ly: reachable from Sol {len(idx_seen)}/{len(cat)} "
              f"({100*len(idx_seen)/len(cat):.0f}%)")
    return cat

cat_a = build(100, 8.0, "A: 100ly bubble")
cat_b = build(125, 6.5, "B: 125ly bubble")

# spectral mix of catalog A
from collections import Counter
mix = Counter(s['spect'] for s in cat_a)
print("\ncatalog A spectral mix:", dict(mix.most_common(10)))
print("catalog A sample names:",
      [s['name'] for s in cat_a[:6]], '...',
      [s['name'] for s in cat_a if s['q'] == 3][10:16])
