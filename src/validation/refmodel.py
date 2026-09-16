# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 josiaslg <josiaslg@bsd.com.br> - https://github.com/josiaslg/Enhancer-Reloaded
"""Reference model of Enhancer 0.17 mono path, written from ENHANCER_SPEC.md.
Validates against host/out/<cfg>_<sig>.raw produced by the original dsp_enh.dll."""
import math, struct, sys, time

PI = math.pi

def g_slider(s):
    return 1.0 - math.log10(1 + 9 * (100 - s) * 0.01)

def rlc_section(dt, k, C, cap, d, e):
    """coefficients of one resonant 2-pole LP from the RLC prototype"""
    p = 1.0 / (k * C)
    den = (e + p) * d + dt
    b = dt / den
    a1 = (d * p + (p * cap) / dt + 20 * cap) / den
    a2 = (p * cap) / (den * dt)
    return b, a1, a2

class Enhancer:
    def __init__(self, fs=44100, hb=0, hbr=0, db=0, dbr=0, tr=0, trr=0, amb=0, ambr=0, boost=0, dry=100, vol=1.0):
        self.fs = fs; dt = 1.0 / fs
        # sliders
        self.hb_on = hb > 0; self.hb_gain = g_slider(hb); self.hb_range = 1 + 0.02 * hbr
        self.db_on = db > 0; self.db_gain = 2 * g_slider(db); db_r = 1 - 0.01 * dbr
        self.tr_on = tr > 0; self.tr_gain = g_slider(tr); t = 1 - 0.02 * trr
        self.amb_on = amb > 0; self.rev_dry = 1 - amb * 0.01 * 0.5; self.wet = amb * 0.01 * 0.027; r = ambr * 0.01
        self.boost = boost; self.dry = 1.0 if dry == 100 else 10 ** ((0.2 * dry - 20) / 20)
        self.vol = vol; self.auto = 1.0
        # DC block + HPF60
        self.dc_c = 1.0 / (PI * dt * 10 + 1)
        w = PI * 120 / fs; sn, cs = math.sin(w), math.cos(w)
        a0 = 1.0 / (sn * 0.5263157894736842 + 1)
        self.h_a1 = -2 * cs * a0; self.h_a2 = (1 - sn * 0.5263157894736842) * a0
        self.h_b1 = -((cs + 1) * (10 ** 0.05) * a0); self.h_b0 = self.h_b1 * -0.5
        # harmonic bass LPs
        k = PI * PI * 2e-6; d = (dt + 0.001) / (dt * 500); e = dt * 20
        self.lp1 = rlc_section(dt, k, 62500, 2e-6, d, e)
        self.lp2 = rlc_section(dt, k, 40000, 2e-6, d, e)
        d3 = (dt + 0.0025) / (dt * 500)
        p = 1.0 / (PI * PI * 0.2); den = (e + p) * d3 + dt
        self.lp3 = (dt / den, (d3 * p + (p * 5e-6) / dt + 1e-4) / den, (p * 5e-6) / (den * dt))
        self.lp4 = rlc_section(dt, k, 25600, 2e-6, d, e)
        # drum bass
        R = 115 - db_r * 25; L = db_r * 300 + 1000
        p = 1.0 / (k * R * R * 4); dd = (L * 2e-6 + dt) / (L * dt); den = (e + p) * dd + dt
        self.dbc = (dt / den, (dd * p + (p * 2e-6) / dt + 4e-5) / den, (p * 2e-6) / (den * dt))
        # treble
        c = 1.0 / (abs(t) * 2 + 2)
        self.fir = (-t * c, c, t * c, -c); self.tr_delay_idx = 3 if t > 0 else 2
        self.tr_scale = 1.5 * PI / 32768
        # reverb
        self.cL = [2202, 2529, 2910, 3340, 3840]
        self.c_rd = [0] * 5; self.c_wr = [1116, 1253, 1484, 1659, 1946]
        self.c_buf = [[0.0] * (n + 1) for n in self.cL]; self.c_st = [0.0] * 5
        self.damp_a = r * 0.3 + 0.4; self.damp_b = 1 - self.damp_a
        self.c_fb = [0.01 ** (n / ((1 + 3 * r) * fs)) for n in self.cL]
        self.apL = [339, 398, 478, 728]; self.apg = [0.63, 0.57, 0.52, 0.48]
        self.ap_idx = [0] * 4; self.ap_buf = [[0.0] * (n + 1) for n in self.apL]
        # output stage
        self.recover = 10 ** (1.0 / (fs * 20))
        self.TH = 32685.78431372549
        self.tree = [1.0] * (512 * 11); self.sib = [i + 1 if i % 2 == 0 else i - 1 for i in range(1024)]
        self.hist = [0.0] * 512; self.gsum = 0.0; self.cnt = 1000; self.p = 0
        self.dline = [0.0] * 512
        self.b_lp = 1.0 / (PI * dt * 500 + 1); self.b_c = 0.34 / (self.b_lp / (1 - self.b_lp)); self.b_env = 0.0
        self.btab = self._boost_table(4.0)
        # states
        self.x_prev = 0.0; self.xd_prev = 0.0
        self.s1 = [0.0, 0.0]; self.s2 = [0.0, 0.0]; self.s3 = [0.0, 0.0]; self.s4 = [0.0, 0.0]
        self.hx = [0.0, 0.0]; self.hy = [0.0, 0.0]
        self.d1 = [0.0, 0.0]; self.d2 = [0.0, 0.0]
        self.th = [0.0, 0.0, 0.0]

    def _boost_table(self, amount):
        amount = min(10.0, max(1.0, amount))
        gmax = 10 ** (amount * 0.05); fmax = gmax * 32750; a0 = math.asin(0.6106870229007634)
        phi = PI * 0.5 + PI * 5000 / fmax - a0
        tab = [0] * 32769
        for i in range(32769):
            y = i * gmax
            if y > 20000:
                y = math.sin((y - 20000) * phi / (fmax - 20000) + a0) * 32750
            tab[i] = int(y)  # truncation
        return tab

    @staticmethod
    def sec(coef, st, x):
        b, a1, a2 = coef
        y = b * x + a1 * st[0] - a2 * st[1]
        st[1] = st[0]; st[0] = y
        return y

    def process(self, samples):
        out = []
        for smp in samples:
            x = float(smp)
            xd = ((x - self.x_prev) + self.xd_prev) * self.dc_c
            self.x_prev = x
            sig = x; bass = 0.0
            if self.hb_on:
                l1 = self.sec(self.lp1, self.s1, xd)
                l2 = self.sec(self.lp2, self.s2, l1)
                v = self.hb_range * l2; w = 6e-05 * v
                if v <= 0:
                    h = -(math.log10(1 + 9 * math.log10(1 - w)) * 150000.0)
                else:
                    h = math.log10(1 + 9 * math.log10(1 + w)) * 150000.0
                l3 = self.sec(self.lp3, self.s3, self.hb_gain * h)
                bass = self.sec(self.lp4, self.s4, l3)
                hx, hy = self.hx, self.hy
                xh = hx[0] * self.h_b1 + (hx[1] + xd) * self.h_b0 - hy[0] * self.h_a1 - hy[1] * self.h_a2
                hx[1] = hx[0]; hx[0] = xd; hy[1] = hy[0]; hy[0] = xh
                sig = xh
            self.xd_prev = xd
            if self.db_on:
                a = self.sec(self.dbc, self.d1, self.db_gain * sig)
                b = self.sec(self.dbc, self.d2, a)
                bass += a - b
            treb = 0.0
            if self.tr_on:
                th = self.th
                f = self.fir[0] * sig + self.fir[1] * th[0] + self.fir[2] * th[1] + self.fir[3] * th[2]
                treb = math.sin(self.tr_scale * f) * self.tr_gain * 49152.0
                th[2] = th[1]; th[1] = th[0]; th[0] = sig
                sig = th[self.tr_delay_idx - 1]
            sig = sig * self.dry + treb
            if self.amb_on:
                acc = 0.0
                for i in range(5):
                    rd = self.c_rd[i]; wr = self.c_wr[i]; buf = self.c_buf[i]
                    st = self.damp_b * self.c_st[i] + self.damp_a * buf[rd]
                    self.c_st[i] = st
                    buf[rd] = st * self.c_fb[i] + sig
                    buf[wr] += sig
                    rd += 1; wr += 1
                    self.c_rd[i] = 0 if rd > self.cL[i] else rd
                    self.c_wr[i] = 0 if wr > self.cL[i] else wr
                    acc += st
                xin = acc
                for j in range(4):
                    i = self.ap_idx[j]; buf = self.ap_buf[j]
                    t = buf[i]; o = t - xin; buf[i] = t * self.apg[j] + xin; xin = o
                    i += 1; self.ap_idx[j] = 0 if i > self.apL[j] else i
                sig = self.wet * xin + sig * self.rev_dry
            y = (sig + bass) * self.vol * self.auto
            ay = abs(y)
            if ay <= 29000.0:
                if self.auto < 1.0:
                    self.auto *= self.recover
            else:
                self.auto *= 48.333333333333336 / ay + 0.9983333333333333
            # lookahead limiter
            p = self.p; tree = self.tree; prop = False
            if ay <= self.TH:
                self.cnt -= 1
                tree[p * 11] = 1.0
                if self.cnt < 0:
                    self.cnt = 0
                elif self.cnt > 0:
                    prop = True
            else:
                self.cnt = 0x3fc
                tree[p * 11] = self.TH / ay
                prop = True
            if prop:
                root = tree[9]
                idx = p
                for lvl in range(9):
                    cur = idx * 11 + lvl; sb = self.sib[idx] * 11 + lvl
                    idx >>= 1
                    tree[idx * 11 + lvl + 1] = tree[sb] if tree[sb] <= tree[cur] else tree[cur]
                self.gsum += root - self.hist[p]; self.hist[p] = root
            delayed = self.dline[p + 1]
            p += 1; self.dline[p] = y
            if p > 510: p = 0
            self.p = p
            o = delayed * (self.gsum / 510.0)
            if self.boost:
                tmp = self.b_lp * self.b_env
                self.b_env = tmp + o
                v = o - self.b_c * tmp
                i = int(v - 33000.0)  # trunc toward zero
                k = i + 33000         # table index from center (T[center-4k] = f(k))
                val = self.btab[k] if k >= 0 else -self.btab[-k]
                s = ((val + 32768) & 0xffff) - 32768
                out.append(s)
            else:
                s = int(o)
                s = ((s + 32768) & 0xffff) - 32768
                out.append(s)
        return out

def load(fn):
    b = open(fn, 'rb').read(); return struct.unpack('<%dh' % (len(b) // 2), b)

if __name__ == "__main__":
    cfgs = {"off": {}, "hb50": dict(hb=50, hbr=50), "hb100": dict(hb=100, hbr=100), "db50": dict(db=50, dbr=50),
            "tr50": dict(tr=50, trr=50), "amb50": dict(amb=50, ambr=50), "boost": dict(boost=1)}
    tests = [("off", "s40"), ("hb50", "s40"), ("hb100", "mix"), ("db50", "burst"), ("tr50", "s1k"), ("tr50", "imp"), ("amb50", "imp"), ("amb50", "mix"), ("boost", "s1k"), ("boost", "mix")]
    if len(sys.argv) > 1: tests = [tuple(a.split(":")) for a in sys.argv[1:]]
    for cfg, sig in tests:
        t0 = time.time()
        ref = load(f"out/{cfg}_{sig}.raw"); inp = load(f"in_{sig}.raw")
        m = Enhancer(**cfgs[cfg]); y = m.process(inp)
        diffs = [abs(a - b) for a, b in zip(y, ref)]
        mx = max(diffs); n1 = sum(1 for d in diffs if d > 1); n8 = sum(1 for d in diffs if d > 8)
        pk = max(abs(v) for v in ref)
        print(f"{cfg:6s} {sig:6s} peak={pk:6d} maxdiff={mx:6d} >1LSB={n1:6d} >8LSB={n8:6d}  ({time.time()-t0:.1f}s)")
