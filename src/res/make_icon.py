# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 josiaslg <josiaslg@bsd.com.br> - https://github.com/josiaslg/Enhancer-Reloaded
# make_icon.py - renders the "purple hat" icon (Enhancer Reloaded) as a multi-size .ico (PNG entries), pure Python.
import math, struct, zlib, sys

def render(size, gray=False, ss=6):
    """Draw a tilted purple fedora on a transparent background. Supersampled by `ss`, returns RGBA rows."""
    S = size * ss
    px = [[(0, 0, 0, 0)] * S for _ in range(S)]
    crown = (156, 58, 214); crown_hi = (196, 118, 240); band = (46, 18, 70); brim = (128, 40, 186); brim_lo = (92, 26, 140); edge = (30, 10, 50)
    if gray:
        def g(c): v = int(0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]); return (v, v, v)
        crown, crown_hi, band, brim, brim_lo, edge = map(g, (crown, crown_hi, band, brim, brim_lo, edge))
    cx, cy = S * 0.5, S * 0.56
    tilt = math.radians(-12)
    ct, st = math.cos(tilt), math.sin(tilt)
    def local(x, y):  # rotate into hat space
        dx, dy = x - cx, y - cy
        return dx * ct + dy * st, -dx * st + dy * ct
    for y in range(S):
        for x in range(S):
            lx, ly = local(x + 0.5, y + 0.5)
            u, v = lx / S, ly / S  # normalized, hat centred at (0,0)
            col = None
            # brim: ellipse, thick
            if (u / 0.46) ** 2 + (v / 0.13) ** 2 <= 1.0:
                col = brim if v < 0.02 else brim_lo
                if (u / 0.43) ** 2 + (v / 0.10) ** 2 > 1.0: col = edge
            # crown: rounded trapezoid above the brim
            top, bot = -0.40, 0.02
            if bot >= v >= top:
                t = (v - top) / (bot - top)            # 0 at top, 1 at bottom
                half = 0.20 + 0.10 * t                  # widens toward the brim
                r = 0.10                                # corner rounding at the top
                inside = abs(u) <= half
                if v < top + r:  # rounded top corners
                    ex = max(0.0, abs(u) - (half - r)); ey = (top + r) - v
                    inside = inside and (ex * ex + ey * ey <= r * r)
                if inside:
                    col = crown_hi if u < -0.08 and v < -0.15 else crown
                    if -0.09 <= v <= -0.02: col = band     # hat band
                    if abs(u) > half - 0.018 or v < top + 0.018: col = edge if v > top + r * 0.3 or abs(u) > half - 0.02 else col
            if col: px[y][x] = (col[0], col[1], col[2], 255)
    # downsample
    out = []
    for y in range(size):
        row = []
        for x in range(size):
            r = g = b = a = 0
            for yy in range(ss):
                for xx in range(ss):
                    p = px[y * ss + yy][x * ss + xx]; r += p[0] * p[3]; g += p[1] * p[3]; b += p[2] * p[3]; a += p[3]
            n = ss * ss
            if a: row.append((r // a, g // a, b // a, a // n))
            else: row.append((0, 0, 0, 0))
        out.append(row)
    return out

def png(rows):
    h = len(rows); w = len(rows[0])
    raw = b''.join(b'\0' + b''.join(bytes(p) for p in r) for r in rows)
    def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')

def ico(sizes, gray, path):
    imgs = [png(render(s, gray)) for s in sizes]
    hdr = struct.pack('<HHH', 0, 1, len(sizes)); off = 6 + 16 * len(sizes); entries = b''
    for s, im in zip(sizes, imgs):
        entries += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(im), off); off += len(im)
    open(path, 'wb').write(hdr + entries + b''.join(imgs))

if __name__ == '__main__':
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    ico(sizes, False, 'hat.ico'); ico(sizes, True, 'hat_off.ico')
    open('hat_preview.png', 'wb').write(png(render(128)))
    print('icons written')
