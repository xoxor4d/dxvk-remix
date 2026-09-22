#!/usr/bin/env python3
"""Regenerate src/dxvk/rtx_render/rtx_star_catalogue_data.h from the Bright
Star Catalogue.

    curl -sSL -o catalog.gz https://cdsarc.cds.unistra.fr/ftp/V/50/catalog.gz
    gunzip -c catalog.gz > bsc5.dat
    python scripts/gen_star_catalogue.py bsc5.dat

The source is measured astronomical data from CDS/VizieR V/50 (Hoffleit &
Warren 1991), redistributed with citation. Deliberately not the HYG merge,
which is CC BY-SA and would make the generated table an adapted work.

Run with no --emit to only parse and validate, which is worth doing if the
source is ever re-downloaded: the record layout is fixed-width with no
delimiters, so a wrong offset produces plausible-looking garbage rather than an
error. The validation compares six stars against independently known V and B-V
and checks the brightness ordering of the six brightest.
"""

import math, sys

SRC = "bsc5.dat"

def parse():
    out = []
    skipped = 0
    for line in open(SRC, encoding="latin-1"):
        if len(line) < 115:
            skipped += 1
            continue
        try:
            rah = float(line[75:77]); ram = float(line[77:79]); ras = float(line[79:83])
            sign = -1.0 if line[83] == '-' else 1.0
            ded = float(line[84:86]); dem = float(line[86:88]); des = float(line[88:90])
            vmag = float(line[102:107])
        except ValueError:
            skipped += 1          # novae and a few records carry no position
            continue
        try:
            bv = float(line[109:114])
        except ValueError:
            bv = 0.58             # unmeasured: assume solar colour
        hr = line[0:4].strip()
        name = line[4:14].strip()
        ra = math.radians((rah + ram/60.0 + ras/3600.0) * 15.0)
        dec = sign * math.radians(ded + dem/60.0 + des/3600.0)
        out.append((hr, name, ra, dec, vmag, bv))
    return out, skipped

stars, skipped = parse()
print(f"parsed {len(stars)} stars, skipped {skipped} records without a position/magnitude")

mags = sorted(s[4] for s in stars)
print(f"V magnitude range {mags[0]:.2f} .. {mags[-1]:.2f}   median {mags[len(mags)//2]:.2f}")
bvs = sorted(s[5] for s in stars)
print(f"B-V range {bvs[0]:.2f} .. {bvs[-1]:.2f}")

# Independent check: these five have well known V and B-V.
KNOWN = {
    "2491": ("Sirius",     -1.46, 0.00),
    "7001": ("Vega",        0.03, 0.00),
    "2061": ("Betelgeuse",  0.50, 1.85),
    "1457": ("Aldebaran",   0.85, 1.54),
    "1708": ("Capella",     0.08, 0.80),
    "5340": ("Arcturus",   -0.04, 1.23),
}
print("\nvalidation against independently known values:")
ok = True
by_hr = {s[0]: s for s in stars}
for hr, (nm, v_exp, bv_exp) in KNOWN.items():
    s = by_hr.get(hr)
    if not s:
        print(f"  HR {hr} {nm}: NOT FOUND"); ok = False; continue
    dv, dbv = abs(s[4]-v_exp), abs(s[5]-bv_exp)
    flag = "ok" if (dv <= 0.06 and dbv <= 0.06) else "MISMATCH"
    if flag != "ok": ok = False
    print(f"  HR {hr:<5} {nm:<11} V={s[4]:>6.2f} (exp {v_exp:>6.2f})  "
          f"B-V={s[5]:>5.2f} (exp {bv_exp:>5.2f})  {flag}")
print("parse validated" if ok else "PARSE IS WRONG")

# Brightest few, as a second sanity check on the magnitude column.
print("\nbrightest 6 by V (should be Sirius, Canopus, Arcturus, Vega, Capella, Rigel):")
for s in sorted(stars, key=lambda s: s[4])[:6]:
    print(f"  HR {s[0]:<5} {s[1]:<11} V={s[4]:>6.2f}")

if "--emit" in sys.argv:
    # Packed: RA as uint16 over [0,2pi), Dec as int16 over [-pi/2,pi/2],
    # V as uint8 over [-2,8.2], B-V as uint8 over [-0.5,5.875]. The colour
    # range has to reach 5.74: a few catalogue stars are that red, and packing
    # for 2.5 silently clamped them by over 3 in B-V.
    # 20 arcsec of position, 0.04 mag, 0.012 in colour: all far inside a pixel
    # (~130 arcsec) and inside what the eye separates in brightness.
    recs = []
    for hr, nm, ra, dec, v, bv in stars:
        ra_u = int(round((ra % (2*math.pi)) / (2*math.pi) * 65535.0)) & 0xFFFF
        dec_i = max(-32767, min(32767, int(round(dec / (math.pi/2) * 32767.0))))
        v_u = max(0, min(255, int(round((v + 2.0) * 25.0))))
        bv_u = max(0, min(255, int(round((bv + 0.5) * 40.0))))
        recs.append((ra_u, dec_i, v_u, bv_u))
    print(f"\nemitting {len(recs)} records, {len(recs)*6} bytes packed")
    with open("bsc5_packed.txt", "w") as f:
        for r in recs:
            f.write("%d %d %d %d\n" % r)
    # round-trip error check
    worst_pos = worst_v = worst_bv = 0.0
    for (hr, nm, ra, dec, v, bv), (ra_u, dec_i, v_u, bv_u) in zip(stars, recs):
        ra2 = ra_u / 65535.0 * 2*math.pi
        dec2 = dec_i / 32767.0 * (math.pi/2)
        # angular separation
        d = math.acos(max(-1.0, min(1.0,
            math.sin(dec)*math.sin(dec2) + math.cos(dec)*math.cos(dec2)*math.cos(ra-ra2))))
        worst_pos = max(worst_pos, math.degrees(d)*3600.0)
        worst_v = max(worst_v, abs(v - (v_u/25.0 - 2.0)))
        worst_bv = max(worst_bv, abs(bv - (bv_u/40.0 - 0.5)))
    print(f"round-trip worst case: position {worst_pos:.1f} arcsec, "
          f"V {worst_v:.3f} mag, B-V {worst_bv:.4f}")
