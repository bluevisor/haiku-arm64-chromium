"""Build a Haiku vector icon (HVIF) of the Chromium logo from product_logo_256.png.

usage: python3 -I mkhvif.py logo256.png chrome.hvif
The PNG only supplies the colours and the sector boundary angles; the shapes
are drawn as polygons on a 64x64 icon grid (centre 32, radius 30).
"""
import math, struct, sys, zlib

# --- PNG decode (8-bit RGBA, non-interlaced) --------------------------------
d = open(sys.argv[1], "rb").read(); p = 8; idat = b""
while p < len(d):
    n = struct.unpack(">I", d[p:p + 4])[0]; t = d[p + 4:p + 8]; c = d[p + 8:p + 8 + n]
    if t == b"IHDR": w, h, bd, ct = struct.unpack(">IIBB", c[:10])
    if t == b"IDAT": idat += c
    p += 12 + n
raw = zlib.decompress(idat); bpp = 4; stride = w * bpp
rows = []; prev = bytearray(stride); pos = 0
for y in range(h):
    f = raw[pos]; line = bytearray(raw[pos + 1:pos + 1 + stride]); pos += 1 + stride
    for i in range(stride):
        a = line[i - bpp] if i >= bpp else 0; b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
        if f == 1: line[i] = (line[i] + a) & 255
        elif f == 2: line[i] = (line[i] + b) & 255
        elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
        elif f == 4:
            pa = abs(b - c); pb = abs(a - c); pc = abs(a + b - 2 * c)
            line[i] = (line[i] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
    rows.append(bytes(line)); prev = line

def px(x, y):
    o = rows[int(y)][int(x) * bpp:int(x) * bpp + 3]; return tuple(o)

R = w / 2; C = (R, R)
def at(r, deg):  # y-down angle in degrees
    return px(C[0] + r * math.cos(math.radians(deg)), C[1] + r * math.sin(math.radians(deg)))

def transitions(r):
    out = []; last = at(r, 359.5)
    for i in range(720):
        deg = i / 2; c = at(r, deg)
        if c != last:
            out.append((deg, last, c)); last = c
    return out

def cluster(angles):
    # anti-aliased edges give a run of transitions; keep the middle of each run
    groups = []
    for a in sorted(angles):
        if groups and a - groups[-1][-1] <= 3: groups[-1].append(a)
        else: groups.append([a])
    return [(g[0] + g[-1]) / 2 for g in groups]
corners = cluster(t[0] for t in transitions(R * 0.92))
ringpts = cluster(t[0] for t in transitions(R * 0.56))  # just outside the white ring
assert len(corners) == 3, corners
assert len(ringpts) == 3, ringpts
# pair each corner with the nearest ring point (the boundary line)
def nearest(a, pts):
    return min(pts, key=lambda q: min(abs(q - a), 360 - abs(q - a)))
bounds = [(c, nearest(c, ringpts)) for c in corners]
print("boundaries (corner deg, ring deg):", bounds)

white = at(R * 0.5 * 0.9, 0)
centre = px(*C)
sector_cols = [at(R * 0.8, (bounds[i][0] + bounds[(i + 1) % 3][0] + (360 if i == 2 else 0)) / 2 % 360) for i in range(3)]
print("colours:", sector_cols, "ring", white, "centre", centre)

# --- geometry on the icon grid ----------------------------------------------
cx = cy = 32.0; Ro = 30.0; Rring = Ro * 0.5; Rin = Ro * 0.406
def pt(r, deg):
    return (cx + r * math.cos(math.radians(deg)), cy + r * math.sin(math.radians(deg)))
def arc(r, a0, a1, n):
    return [pt(r, a0 + (a1 - a0) * i / n) for i in range(n + 1)]

paths = []
for i in range(3):
    c0, r0 = bounds[i]; c1, r1 = bounds[(i + 1) % 3]
    if c1 < c0: c1 += 360
    poly = arc(Ro, c0, c1, 40)           # outer rim
    poly.append(pt(Rring * 0.98, r1))    # down the boundary to the ring
    poly.append((cx, cy))                # through the centre (hidden by the discs)
    poly.append(pt(Rring * 0.98, r0))
    paths.append(poly)
paths.append(arc(Rring, 0, 360, 72)[:-1])
paths.append(arc(Rin, 0, 360, 72)[:-1])

# --- HVIF encode -------------------------------------------------------------
def coord(v):
    n = round((v + 128.0) * 102.0)
    return bytes([0x80 | (n >> 8), n & 0xff])
out = bytearray(b"ncif")
styles = sector_cols + [white, centre]
out.append(len(styles))
for (r, g, b) in styles:
    out += bytes([0x01, r, g, b, 0xff])
out.append(len(paths))
for poly in paths:
    out += bytes([0x0a, len(poly)])      # closed, no curves
    for (x, y) in poly:
        out += coord(x) + coord(y)
out.append(len(paths))
for i in range(len(paths)):
    out += bytes([0x0a, i, 1, i, 0x00])  # path source, style i, 1 path, no transform
open(sys.argv[2], "wb").write(out)
print(len(out), "bytes ->", sys.argv[2])
