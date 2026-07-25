#!/usr/bin/env python3
"""Curate the HYG catalog into Solfarer's Local Bubble and emit the packed
resource + generated header.

Downloads hygdata_v41.csv on first run (34 MB, gitignored). Outputs:
  resources/data/stars.bin  — uint16 count, count x 12-byte records sorted by
                              distance from Sol, then NUL-terminated names
  src/c/catalog_gen.h       — record struct, class letters, constellation table

Record (little-endian, 12 bytes):
  int16  x, y, z      galactic, 0.01 ly units
  uint16 name_off     offset into the names blob
  int8   absmag4      absolute magnitude x 4
  uint8  spect        high nibble: index into CAT_CLASSES; low: subclass digit
  uint8  con          index into CAT_CONS, 0xFF none
  uint8  reserved
"""
import csv, math, os, struct, sys, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CSV = os.path.join(HERE, 'hygdata_v41.csv')
URL = ('https://raw.githubusercontent.com/astronexus/HYG-Database/main/'
       'hyg/CURRENT/hygdata_v41.csv')

LY_PER_PC = 3.26156
MAX_LY = 100.0
NEAR_KEEP_LY = 50.0        # inside this, keep everything
DIM_CAP_ABSMAG = 8.0       # beyond, drop dim anonymous stars
CLASSES = "OBAFGKMD?"

R = [[-0.0548755604, -0.8734370902, -0.4838350155],
     [ 0.4941094279, -0.4448296300,  0.7469822445],
     [-0.8676661490, -0.1980763734,  0.4559837762]]

GREEK = {
 'Alp':'Alpha','Bet':'Beta','Gam':'Gamma','Del':'Delta','Eps':'Epsilon',
 'Zet':'Zeta','Eta':'Eta','The':'Theta','Iot':'Iota','Kap':'Kappa',
 'Lam':'Lambda','Mu':'Mu','Nu':'Nu','Xi':'Xi','Omi':'Omicron','Pi':'Pi',
 'Rho':'Rho','Sig':'Sigma','Tau':'Tau','Ups':'Upsilon','Phi':'Phi',
 'Chi':'Chi','Psi':'Psi','Ome':'Omega'}

# The 88 IAU constellations — HYG stores only the abbreviation; the full
# names are a closed set, so they ride along in the generated header.
CON_FULL = {
 'And':'Andromeda','Ant':'Antlia','Aps':'Apus','Aql':'Aquila','Aqr':'Aquarius',
 'Ara':'Ara','Ari':'Aries','Aur':'Auriga','Boo':'Bootes','CMa':'Canis Major',
 'CMi':'Canis Minor','CVn':'Canes Venatici','Cae':'Caelum',
 'Cam':'Camelopardalis','Cap':'Capricornus','Car':'Carina','Cas':'Cassiopeia',
 'Cen':'Centaurus','Cep':'Cepheus','Cet':'Cetus','Cha':'Chamaeleon',
 'Cir':'Circinus','Cnc':'Cancer','Col':'Columba','Com':'Coma Berenices',
 'CrA':'Corona Australis','CrB':'Corona Borealis','Crt':'Crater','Cru':'Crux',
 'Crv':'Corvus','Cyg':'Cygnus','Del':'Delphinus','Dor':'Dorado','Dra':'Draco',
 'Equ':'Equuleus','Eri':'Eridanus','For':'Fornax','Gem':'Gemini','Gru':'Grus',
 'Her':'Hercules','Hor':'Horologium','Hya':'Hydra','Hyi':'Hydrus',
 'Ind':'Indus','LMi':'Leo Minor','Lac':'Lacerta','Leo':'Leo','Lep':'Lepus',
 'Lib':'Libra','Lup':'Lupus','Lyn':'Lynx','Lyr':'Lyra','Men':'Mensa',
 'Mic':'Microscopium','Mon':'Monoceros','Mus':'Musca','Nor':'Norma',
 'Oct':'Octans','Oph':'Ophiuchus','Ori':'Orion','Pav':'Pavo','Peg':'Pegasus',
 'Per':'Perseus','Phe':'Phoenix','Pic':'Pictor','PsA':'Piscis Austrinus',
 'Psc':'Pisces','Pup':'Puppis','Pyx':'Pyxis','Ret':'Reticulum',
 'Scl':'Sculptor','Sco':'Scorpius','Sct':'Scutum','Ser':'Serpens',
 'Sex':'Sextans','Sge':'Sagitta','Sgr':'Sagittarius','Tau':'Taurus',
 'Tel':'Telescopium','TrA':'Triangulum Australe','Tri':'Triangulum',
 'Tuc':'Tucana','UMa':'Ursa Major','UMi':'Ursa Minor','Vel':'Vela',
 'Vir':'Virgo','Vol':'Volans','Vul':'Vulpecula'}


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
        return (gl if gl.startswith(('GJ', 'Gl', 'NN', 'Wo')) else 'GJ ' + gl), 1
    if r['hip']:
        return 'HIP ' + r['hip'], 0
    if r['hd']:
        return 'HD ' + r['hd'], 0
    return 'Star ' + r['id'], 0


def main():
    if not os.path.exists(CSV):
        print('downloading HYG v4.1 (34 MB)...')
        urllib.request.urlretrieve(URL, CSV)

    stars = []
    with open(CSV) as f:
        for r in csv.DictReader(f):
            if r['id'] == '0':
                continue                       # Sol is index -1 in the app
            try:
                dpc = float(r['dist'])
            except ValueError:
                continue
            if dpc <= 0 or dpc >= 90000:
                continue
            dly = dpc * LY_PER_PC
            if dly > MAX_LY:
                continue
            xe, ye, ze = float(r['x']), float(r['y']), float(r['z'])
            x = (R[0][0]*xe + R[0][1]*ye + R[0][2]*ze) * LY_PER_PC
            y = (R[1][0]*xe + R[1][1]*ye + R[1][2]*ze) * LY_PER_PC
            z = (R[2][0]*xe + R[2][1]*ye + R[2][2]*ze) * LY_PER_PC
            name, q = pick_name(r)
            absmag = float(r['absmag']) if r['absmag'] else 10.0
            stars.append({
                'name': name, 'q': q, 'x': x, 'y': y, 'z': z, 'dly': dly,
                'mag': float(r['mag']) if r['mag'] else 15.0,
                'absmag': absmag, 'spect': r['spect'].strip(),
                'con': r['con'].strip(),
                'base': r['base'].strip() or r['id'],
            })

    # dedupe systems: brightest per base id, then 0.15 ly proximity merge
    by_base = {}
    for s in stars:
        k = s['base']
        if k not in by_base or s['mag'] < by_base[k]['mag']:
            by_base[k] = s
    merged = []
    for s in sorted(by_base.values(), key=lambda s: s['dly']):
        if any(abs(s['x']-t['x']) < 0.15 and abs(s['y']-t['y']) < 0.15 and
               abs(s['z']-t['z']) < 0.15 for t in merged):
            continue
        merged.append(s)

    cat = [s for s in merged if s['dly'] <= NEAR_KEEP_LY or
           s['absmag'] <= DIM_CAP_ABSMAG or s['q'] >= 2]

    # connectivity from Sol at 20 ly: drop anything the walk can never reach
    HOP = 20.0
    seen, frontier = set(), [(0.0, 0.0, 0.0)]
    pts = [(s['x'], s['y'], s['z']) for s in cat]
    while frontier:
        cx, cy, cz = frontier.pop()
        for i, (px, py, pz) in enumerate(pts):
            if i in seen:
                continue
            if (px-cx)**2 + (py-cy)**2 + (pz-cz)**2 <= HOP*HOP:
                seen.add(i)
                frontier.append((px, py, pz))
    stranded = len(cat) - len(seen)
    if stranded:
        cat = [s for i, s in enumerate(cat) if i in seen]
        print(f"dropped {stranded} stars unreachable at {HOP} ly hops")

    cons = sorted({s['con'] for s in cat if s['con']})
    con_idx = {c: i for i, c in enumerate(cons)}
    assert len(cons) < 255

    def spect_byte(sp):
        cls = CLASSES.index(sp[0].upper()) if sp and sp[0].upper() in CLASSES \
              else CLASSES.index('?')
        sub = 5
        for ch in sp[1:3]:
            if ch.isdigit():
                sub = int(ch)
                break
        return (cls << 4) | sub

    names_blob = bytearray()
    recs = bytearray()
    for s in cat:
        off = len(names_blob)
        assert off < 65535
        names_blob += s['name'].encode('latin-1', 'replace')[:23] + b'\0'
        am4 = max(-128, min(127, round(s['absmag'] * 4)))
        recs += struct.pack('<hhhHbBBB',
                            round(s['x'] * 100), round(s['y'] * 100),
                            round(s['z'] * 100), off, am4,
                            spect_byte(s['spect']),
                            con_idx.get(s['con'], 255), 0)

    blob = struct.pack('<H', len(cat)) + bytes(recs) + bytes(names_blob)
    out_bin = os.path.join(ROOT, 'resources', 'data', 'stars.bin')
    os.makedirs(os.path.dirname(out_bin), exist_ok=True)
    with open(out_bin, 'wb') as f:
        f.write(blob)

    hdr = os.path.join(ROOT, 'src', 'c', 'catalog_gen.h')
    with open(hdr, 'w') as f:
        f.write("// Generated by tools/make_catalog.py — do not edit.\n")
        f.write("#pragma once\n#include <pebble.h>\n\n")
        f.write(f"#define CAT_COUNT {len(cat)}\n")
        f.write("#define CAT_NAMES_OFF (2 + CAT_COUNT * 12)\n")
        f.write(f'#define CAT_CLASSES "{CLASSES}"\n\n')
        f.write("typedef struct __attribute__((packed)) {\n"
                "  int16_t x, y, z;              // 0.01 ly, galactic\n"
                "  uint16_t name_off;\n"
                "  int8_t absmag4;               // absolute magnitude x4\n"
                "  uint8_t spect;                // class nibble | subclass\n"
                "  uint8_t con;                  // index into CAT_CONS, 0xFF none\n"
                "  uint8_t reserved;\n"
                "} StarRec;\n\n")
        f.write(f"#define CAT_N_CONS {len(cons)}\n")
        f.write("static const char CAT_CONS[CAT_N_CONS][4] = {\n  ")
        f.write(', '.join(f'"{c}"' for c in cons))
        f.write("\n};\n")
        f.write("static const char *CAT_CONS_FULL[CAT_N_CONS] = {\n  ")
        f.write(',\n  '.join(f'"{CON_FULL.get(c, c)}"' for c in cons))
        f.write("\n};\n")

    named = sum(1 for s in cat if s['q'] >= 2)
    print(f"catalog: {len(cat)} systems, {len(blob)} bytes "
          f"({len(recs)} records + {len(names_blob)} names)")
    print(f"named (proper/Bayer): {named}; constellations: {len(cons)}")
    print(f"wrote {out_bin}\nwrote {hdr}")


if __name__ == '__main__':
    main()
