// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 josiaslg <josiaslg@bsd.com.br> - https://github.com/josiaslg/Enhancer-Reloaded
// EnhancerDSP.h - clean-room C++ port of the Winamp "Enhancer 0.17" DSP algorithm.
// Written from ENHANCER_SPEC.md (reverse engineered from dsp_enh.dll) and validated
// against the original plugin output (host/refmodel.py is bit-exact for the mono path).
//
// Internal sample scale is +/-32768 (like the original), I/O helpers convert from float [-1,1].
// Differences from the original, on purpose:
//   * any number of channels (bass is shared from the channel mean; L/R reverb lengths alternate)
//   * reverb delay lengths scale with the sample rate (the original hard-codes 44.1 kHz lengths)
//   * output is float, no 16-bit truncation
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdint>

class EnhancerDSP {
public:
    struct Params {
        int volume = 72;      // 0..100  -> 0.4*s-20 dB
        int harmBass = 31;    // 0..100
        int harmBassRange = 50;
        int drumBass = 30;
        int drumBassRange = 50;
        int dry = 100;        // 0..100  -> 0.2*s-20 dB
        int harmTreble = 50;
        int harmTrebleRange = 50;
        int ambience = 0;
        int ambienceRange = 50;
        bool boost = false;
        bool power = true;
    };

    EnhancerDSP() {}

    void prepare(double sampleRate, int channels) {
        fs = sampleRate; nch = std::max(1, channels);
        dt = 1.0 / fs;
        ch.assign(nch, Channel());
        // reverb buffers (lengths scaled with fs)
        const int baseL[5] = {2202, 2529, 2910, 3340, 3840};
        const int baseR[5] = {2862, 3286, 3783, 4342, 4992};
        const int startW[5] = {1116, 1253, 1484, 1659, 1946};
        const int apBase[4] = {339, 398, 478, 728};
        double scale = fs / 44100.0;
        for (int c = 0; c < nch; c++) {
            Channel& C = ch[c];
            const int* L = (c % 2 == 0) ? baseL : baseR;
            for (int i = 0; i < 5; i++) {
                C.combLen[i] = (int)std::lround(L[i] * scale);
                C.comb[i].assign(C.combLen[i] + 1, 0.0);
                C.combRd[i] = 0;
                C.combWr[i] = (int)std::lround(startW[i] * (double)L[i] / baseL[i] * scale) % (C.combLen[i] + 1);
                C.combSt[i] = 0.0;
            }
            for (int j = 0; j < 4; j++) {
                C.apLen[j] = (int)std::lround(apBase[j] * scale);
                C.ap[j].assign(C.apLen[j] + 1, 0.0);
                C.apIdx[j] = 0;
            }
            C.tree.assign(512 * 11, 1.0);
            C.hist.assign(512, 0.0);
            C.dline.assign(512, 0.0);
        }
        for (int i = 0; i < 1024; i++) sib[i] = (i % 2 == 0) ? i + 1 : i - 1;
        buildBoostTable(4.0);
        // fixed coefficients
        dcC = 1.0 / (PI * dt * 10 + 1);
        double w = PI * 120 / fs, sn = std::sin(w), cs = std::cos(w);
        double a0 = 1.0 / (sn * 0.5263157894736842 + 1);
        hA1 = -2 * cs * a0; hA2 = (1 - sn * 0.5263157894736842) * a0;
        hB1 = -((cs + 1) * std::pow(10.0, 0.05) * a0); hB0 = hB1 * -0.5;
        double k = PI * PI * 2e-6, d = (dt + 0.001) / (dt * 500), e = dt * 20;
        rlc(lp1, k, 62500, 2e-6, d, e);
        rlc(lp2, k, 40000, 2e-6, d, e);
        {
            double d3 = (dt + 0.0025) / (dt * 500);
            double p = 1.0 / (PI * PI * 0.2), den = (e + p) * d3 + dt;
            lp3.b = dt / den; lp3.a1 = (d3 * p + (p * 5e-6) / dt + 1e-4) / den; lp3.a2 = (p * 5e-6) / (den * dt);
        }
        rlc(lp4, k, 25600, 2e-6, d, e);
        trScale = 1.5 * PI / 32768.0;
        recover = std::pow(10.0, 1.0 / (fs * 20));
        bLp = 1.0 / (PI * dt * 500 + 1); bC = 0.34 / (bLp / (1 - bLp));
        autoGain = 1.0;
        setParams(params);
    }

    void setParams(const Params& p) {
        params = p;
        double newVol = std::pow(10.0, (0.4 * p.volume - 20) / 20.0);
        if (newVol > vol) { autoGain = 1.0; } else if (vol > 0) { autoGain = std::min(1.0, autoGain * vol / newVol); }
        vol = newVol;
        hbOn = p.harmBass > 0; hbGain = gSlider(p.harmBass); hbRange = 1 + 0.02 * p.harmBassRange;
        dbOn = p.drumBass > 0; dbGain = 2 * gSlider(p.drumBass);
        double dbr = 1 - 0.01 * p.drumBassRange;
        {
            double k = PI * PI * 2e-6, e = dt * 20;
            double R = 115 - dbr * 25, L = dbr * 300 + 1000;
            double pp = 1.0 / (k * R * R * 4), dd = (L * 2e-6 + dt) / (L * dt), den = (e + pp) * dd + dt;
            dbc.b = dt / den; dbc.a1 = (dd * pp + (pp * 2e-6) / dt + 4e-5) / den; dbc.a2 = (pp * 2e-6) / (den * dt);
        }
        trOn = p.harmTreble > 0; trGain = gSlider(p.harmTreble);
        double t = 1 - 0.02 * p.harmTrebleRange, c = 1.0 / (std::fabs(t) * 2 + 2);
        fir[0] = -t * c; fir[1] = c; fir[2] = t * c; fir[3] = -c; trDelay = (t > 0) ? 2 : 1;
        dry = std::pow(10.0, (0.2 * p.dry - 20) / 20.0);
        bool wasAmb = ambOn;
        ambOn = p.ambience > 0; revDry = 1 - p.ambience * 0.01 * 0.5; wet = p.ambience * 0.01 * 0.027;
        double r = p.ambienceRange * 0.01;
        dampA = r * 0.3 + 0.4; dampB = 1 - dampA;
        for (auto& C : ch) for (int i = 0; i < 5; i++) C.combFb[i] = std::pow(0.01, C.combLen[i] / ((1 + 3 * r) * fs));
        if (wasAmb && !ambOn) clearReverb();
    }

    // Interleaved float in/out, [-1,1]. In-place allowed.
    void process(const float* in, float* out, int frames) {
        if (!params.power) { if (in != out) std::copy(in, in + frames * nch, out); return; }
        const double S = 32768.0, invS = 1.0 / 32768.0;
        for (int n = 0; n < frames; n++) {
            // --- DC block per channel, shared bass input
            double xdSum = 0;
            for (int c = 0; c < nch; c++) {
                Channel& C = ch[c];
                double x = in[n * nch + c] * S;
                double xd = ((x - C.xPrev) + C.xdPrev) * dcC;
                C.xPrev = x; C.xdPrev = xd; C.xd = xd; C.sig = x;
                xdSum += xd;
            }
            double bassH = 0.0;
            if (hbOn) {
                double l1 = sec(lp1, s1, xdSum / nch);
                double l2 = sec(lp2, s2, l1);
                double v = hbRange * l2, w = 6e-05 * v, h;
                if (v <= 0) h = -(std::log10(1 + 9 * std::log10(1 - w)) * 150000.0);
                else        h =   std::log10(1 + 9 * std::log10(1 + w)) * 150000.0;
                double l3 = sec(lp3, s3, hbGain * h);
                bassH = sec(lp4, s4, l3);
                for (int c = 0; c < nch; c++) {
                    Channel& C = ch[c];
                    double xh = C.hx[0] * hB1 + (C.hx[1] + C.xd) * hB0 - C.hy[0] * hA1 - C.hy[1] * hA2;
                    C.hx[1] = C.hx[0]; C.hx[0] = C.xd; C.hy[1] = C.hy[0]; C.hy[0] = xh;
                    C.sig = xh;
                }
            }
            // --- per channel: drum bass, treble, dry
            for (int c = 0; c < nch; c++) {
                Channel& C = ch[c];
                double bass = bassH, sig = C.sig;
                if (dbOn) {
                    double a = sec(dbc, C.d1, dbGain * sig);
                    double b = sec(dbc, C.d2, a);
                    bass += a - b;
                }
                double treb = 0.0;
                if (trOn) {
                    double f = fir[0] * sig + fir[1] * C.th[0] + fir[2] * C.th[1] + fir[3] * C.th[2];
                    treb = std::sin(trScale * f) * trGain * 49152.0;
                    C.th[2] = C.th[1]; C.th[1] = C.th[0]; C.th[0] = sig;
                    sig = C.th[trDelay];
                }
                C.sig = sig * dry + treb;
                C.bass = bass;
            }
            // --- reverb (per channel, cross-mixed in pairs)
            if (ambOn) {
                for (int c = 0; c < nch; c++) ch[c].apOut = reverbTick(ch[c], ch[c].sig);
                for (int c = 0; c < nch; c++) {
                    int other = (nch >= 2) ? (c ^ 1) : c;
                    if (other >= nch) other = c;
                    double mine = ch[c].apOut, oth = (other == c) ? 0.0 : ch[other].apOut;
                    ch[c].sig = revDry * ch[c].sig + wet * (mine - 0.5 * oth);
                }
            }
            // --- output gain, auto gain (shared), limiter (per channel state, shared decision on max)
            double ayMax = 0.0;
            for (int c = 0; c < nch; c++) {
                Channel& C = ch[c];
                C.y = (C.sig + C.bass) * vol * autoGain;
                ayMax = std::max(ayMax, std::fabs(C.y));
            }
            if (ayMax <= 29000.0) { if (autoGain < 1.0) autoGain *= recover; }
            else autoGain *= 48.333333333333336 / ayMax + 0.9983333333333333;
            for (int c = 0; c < nch; c++) {
                Channel& C = ch[c];
                double o = limiterTick(C, C.y, ayMax);
                if (params.boost) {
                    double tmp = bLp * C.bEnv;
                    C.bEnv = tmp + o;
                    double v = o - bC * tmp;
                    o = boostShape(v);
                }
                out[n * nch + c] = (float)(o * invS);
            }
        }
    }

    int latencySamples() const { return 511; }
    double getAutoGain() const { return autoGain; }   // 1.0 = no automatic reduction (drives the "max" indicator)
    const Params& getParams() const { return params; }

private:
    static constexpr double PI = 3.141592653589793;
    static constexpr double TH = 32685.78431372549;
    struct Biquad { double b = 0, a1 = 0, a2 = 0; };
    struct Channel {
        double xPrev = 0, xdPrev = 0, xd = 0, sig = 0, bass = 0, y = 0, apOut = 0;
        double hx[2] = {0, 0}, hy[2] = {0, 0};
        double d1[2] = {0, 0}, d2[2] = {0, 0};
        double th[3] = {0, 0, 0};
        std::vector<double> comb[5]; int combLen[5] = {0}, combRd[5] = {0}, combWr[5] = {0}; double combSt[5] = {0}, combFb[5] = {0};
        std::vector<double> ap[4]; int apLen[4] = {0}, apIdx[4] = {0};
        std::vector<double> tree, hist, dline; double gsum = 0; int cnt = 1000, p = 0;
        double bEnv = 0;
    };

    static double gSlider(int s) { return 1.0 - std::log10(1 + 9 * (100 - s) * 0.01); }
    static double sec(const Biquad& q, double* st, double x) {
        double y = q.b * x + q.a1 * st[0] - q.a2 * st[1];
        st[1] = st[0]; st[0] = y; return y;
    }
    void rlc(Biquad& q, double k, double C, double cap, double d, double e) {
        double p = 1.0 / (k * C), den = (e + p) * d + dt;
        q.b = dt / den; q.a1 = (d * p + (p * cap) / dt + 20 * cap) / den; q.a2 = (p * cap) / (den * dt);
    }
    double reverbTick(Channel& C, double sig) {
        double acc = 0.0;
        for (int i = 0; i < 5; i++) {
            int rd = C.combRd[i], wr = C.combWr[i];
            double st = dampB * C.combSt[i] + dampA * C.comb[i][rd];
            C.combSt[i] = st;
            C.comb[i][rd] = st * C.combFb[i] + sig;
            C.comb[i][wr] += sig;
            if (++rd > C.combLen[i]) rd = 0;
            if (++wr > C.combLen[i]) wr = 0;
            C.combRd[i] = rd; C.combWr[i] = wr;
            acc += st;
        }
        double x = acc;
        const double g[4] = {0.63, 0.57, 0.52, 0.48};
        for (int j = 0; j < 4; j++) {
            int i = C.apIdx[j];
            double t = C.ap[j][i], o = t - x;
            C.ap[j][i] = t * g[j] + x; x = o;
            if (++i > C.apLen[j]) i = 0;
            C.apIdx[j] = i;
        }
        return x;
    }
    double limiterTick(Channel& C, double y, double ay) {
        int p = C.p; bool prop = false;
        if (ay <= TH) {
            C.cnt--; C.tree[p * 11] = 1.0;
            if (C.cnt < 0) C.cnt = 0; else if (C.cnt > 0) prop = true;
        } else {
            C.cnt = 0x3fc; C.tree[p * 11] = TH / ay; prop = true;
        }
        if (prop) {
            double root = C.tree[9];
            int idx = p;
            for (int lvl = 0; lvl < 9; lvl++) {
                int cur = idx * 11 + lvl, sb = sib[idx] * 11 + lvl;
                idx >>= 1;
                C.tree[idx * 11 + lvl + 1] = std::min(C.tree[sb], C.tree[cur]);
            }
            C.gsum += root - C.hist[p]; C.hist[p] = root;
        }
        double delayed = C.dline[p + 1];
        p++; C.dline[p] = y;
        if (p > 510) p = 0;
        C.p = p;
        return delayed * (C.gsum / 510.0);
    }
    void buildBoostTable(double amount) {
        amount = std::min(10.0, std::max(1.0, amount));
        double gmax = std::pow(10.0, amount * 0.05), fmax = gmax * 32750, a0 = std::asin(0.6106870229007634);
        double phi = PI * 0.5 + PI * 5000 / fmax - a0;
        boostTab.resize(32769);
        for (int i = 0; i < 32769; i++) {
            double y = i * gmax;
            if (y > 20000) y = std::sin((y - 20000) * phi / (fmax - 20000) + a0) * 32750;
            boostTab[i] = y;
        }
    }
    double boostShape(double v) const {
        // original: integer table lookup with truncation; here linear interpolation of the same curve
        double a = std::fabs(v); if (a >= 32768) a = 32767.999;
        int i = (int)a; double f = a - i;
        double y = boostTab[i] + (boostTab[i + 1] - boostTab[i]) * f;
        return v < 0 ? -y : y;
    }
    void clearReverb() {
        for (auto& C : ch) {
            for (int i = 0; i < 5; i++) { std::fill(C.comb[i].begin(), C.comb[i].end(), 0.0); C.combSt[i] = 0; }
            for (int j = 0; j < 4; j++) std::fill(C.ap[j].begin(), C.ap[j].end(), 0.0);
        }
    }

    Params params;
    double fs = 44100, dt = 1.0 / 44100; int nch = 1;
    std::vector<Channel> ch;
    int sib[1024] = {0};
    double dcC = 1, hA1 = 0, hA2 = 0, hB0 = 0, hB1 = 0;
    Biquad lp1, lp2, lp3, lp4, dbc;
    double s1[2] = {0, 0}, s2[2] = {0, 0}, s3[2] = {0, 0}, s4[2] = {0, 0};
    bool hbOn = false, dbOn = false, trOn = false, ambOn = false;
    double hbGain = 0, hbRange = 1, dbGain = 0, trGain = 0, fir[4] = {0, 0, 0, 0}; int trDelay = 1;
    double trScale = 0, dry = 1, revDry = 1, wet = 0, dampA = 0.4, dampB = 0.6;
    double vol = 1.0, autoGain = 1.0, recover = 1.0, bLp = 0, bC = 0;
    std::vector<double> boostTab;
};
