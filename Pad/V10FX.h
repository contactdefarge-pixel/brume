#pragma once

// Higher quality effects for Hydra (DJM-V10 style): clean and tape delays with Hermite interpolation,
// Dattorro plate reverb (fixed delay lengths: SIZE never bends the pitch), Spiral, Shimmer and Vinyl Brake.
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

namespace v10
{
// --- building blocks ------------------------------------------------------------------------------

// circular buffer with integer, linear and 4-point Hermite reads
struct Line
{
    std::vector<float> b; int w = 0, size = 1;
    void prepare (int n) { size = juce::jmax (8, n); b.assign ((size_t) size, 0.0f); w = 0; }
    void clear() { std::fill (b.begin(), b.end(), 0.0f); }
    inline void push (float x) { b[(size_t) w] = x; if (++w >= size) w = 0; }
    inline float at (int d) const { int i = w - d; while (i < 0) i += size; return b[(size_t) i]; }   // d >= 1
    inline float hermite (double d) const
    {
        double rp = (double) w - d;
        while (rp < 0.0) rp += size;
        const int i1 = (int) rp; const float f = (float) (rp - i1);
        auto s = [&] (int i) { i %= size; if (i < 0) i += size; return b[(size_t) i]; };
        const float y0 = s (i1 - 1), y1 = s (i1), y2 = s (i1 + 1), y3 = s (i1 + 2);
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
};

struct OnePole   // one-pole low-pass; hp() gives the matching high-pass
{
    float a = 0.0f, z = 0.0f;
    void set (double fs, double hz) { a = (float) std::exp (-juce::MathConstants<double>::twoPi * hz / fs); }
    inline float lp (float x) { z = x + (z - x) * a; return z; }
    inline float hp (float x) { return x - lp (x); }
    void clear() { z = 0.0f; }
};

struct Allpass   // Schroeder allpass on a Line (taps readable for the plate outputs)
{
    Line l; int len = 1; float g = 0.5f;
    void prepare (int n, float gain) { len = juce::jmax (2, n); l.prepare (len + 64); g = gain; }
    inline float tick (float x) { const float d = l.at (len); const float v = x - g * d; l.push (v); return d + g * v; }
    inline float tickMod (float x, float exc)   // modulated length, Hermite read
    {
        const float d = l.hermite ((double) len + exc); const float v = x - g * d; l.push (v); return d + g * v;
    }
    void clear() { l.clear(); }
};

// gentle safety on feedback paths: transparent below 0 dBFS, rounds off above (never a hard clip)
inline float safe (float x) { const float a = std::abs (x); return a < 1.0f ? x : (x > 0 ? 1.0f : -1.0f) * (1.0f + std::tanh (a - 1.0f) * 0.5f); }

// --- delays ---------------------------------------------------------------------------------------

// Stereo delay: changing the time slides the read head (tape-like pitch bend, as on the DJM-V10).
// mode 0 = clean digital delay (filtered loop, no saturation), 1 = dub / tape echo (dark, warm, smeared, slight wow).
struct Delay
{
    Line l[2]; OnePole hp[2], lp[2], lp2[2]; Allpass dif[2][2];
    double fs = 48000.0, cur = 4800.0, target = 4800.0, glide = 0.0001;
    float wow = 0.0f, wowInc = 0.0f, spread = 0.0f;
    int mode = 0;

    void prepare (double sampleRate, int m, double maxSeconds = 4.5)
    {
        fs = sampleRate; mode = m;
        for (int c = 0; c < 2; ++c)
        {
            l[c].prepare ((int) (maxSeconds * fs) + 16);
            hp[c].set (fs, mode == 1 ? 190.0 : 70.0);
            lp[c].set (fs, mode == 1 ? 2900.0 : 11000.0);
            lp2[c].set (fs, mode == 1 ? 4200.0 : 16000.0);
            dif[c][0].prepare ((int) ((c ? 0.0079 : 0.0071) * fs), 0.45f);
            dif[c][1].prepare ((int) ((c ? 0.0127 : 0.0113) * fs), 0.4f);
        }
        glide = 1.0 - std::exp (-1.0 / (0.15 * fs));
        wowInc = (float) (juce::MathConstants<double>::twoPi * 0.55 / fs);
        cur = target;
    }
    void clear() { for (int c = 0; c < 2; ++c) { l[c].clear(); hp[c].clear(); lp[c].clear(); lp2[c].clear(); dif[c][0].clear(); dif[c][1].clear(); } }
    void setTime (double samples) { target = juce::jlimit (8.0, (double) l[0].size - 8.0, samples); }
    void setSpread (float s) { spread = s; }   // 0 = straight stereo, 1 = full ping-pong

    // returns the wet signal; fb = feedback (0..0.98)
    inline void process (float inL, float inR, float fb, float& outL, float& outR)
    {
        cur += (target - cur) * glide;
        double d = cur;
        if (mode == 1) { wow += wowInc; if (wow > juce::MathConstants<float>::twoPi) wow -= juce::MathConstants<float>::twoPi; d += std::sin (wow) * 0.0012 * fs; }
        float y[2] = { l[0].hermite (d), l[1].hermite (d) };
        float f[2];
        for (int c = 0; c < 2; ++c)
        {
            float v = lp2[c].lp (lp[c].lp (hp[c].hp (y[c])));
            if (mode == 1) v = std::tanh (dif[c][1].tick (dif[c][0].tick (v)) * 1.15f) / 1.15f;
            f[c] = v * fb;
        }
        // ping-pong blend: the repeats alternate sides as spread goes up
        const float a = 1.0f - spread * 0.5f, b = spread * 0.5f;
        l[0].push (safe (inL + f[0] * a + f[1] * b));
        l[1].push (safe (inR + f[1] * a + f[0] * b));
        outL = y[0]; outR = y[1];
    }
};

// --- Dattorro plate reverb ---------------------------------------------------------------------------
// (J. Dattorro, "Effect Design part 1", 1997). Delay lengths are fixed: decay, damping, pre-delay and input
// diffusion are the controls, so turning SIZE or TIME changes the tail smoothly and never warbles.
struct Plate
{
    double fs = 48000.0;
    Line pre; OnePole bw; Allpass inAp[4];
    Allpass modA, modB, decA, decB;
    Line dA1, dA2, dB1, dB2; OnePole dampA, dampB;
    int lA1 = 1, lA2 = 1, lB1 = 1, lB2 = 1;
    float decay = 0.5f, decayTarget = 0.5f, preLen = 0.0f, preTarget = 0.0f, lfo = 0.0f, lfoInc = 0.0f, exc = 8.0f, gain = 0.6f;
    float fbA = 0.0f, fbB = 0.0f;
    OnePole outHpL, outHpR;
    int tap[14] {};

    int sc (double n) const { return (int) std::round (n * fs / 29761.0); }
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        pre.prepare ((int) (0.25 * fs));
        bw.set (fs, 9000.0);
        const double inL[4] = { 142, 107, 379, 277 }; const float inG[4] = { 0.75f, 0.75f, 0.625f, 0.625f };
        for (int i = 0; i < 4; ++i) inAp[i].prepare (sc (inL[i]), inG[i]);
        modA.prepare (sc (672), -0.7f); modB.prepare (sc (908), -0.7f);
        decA.prepare (sc (1800), 0.5f); decB.prepare (sc (2656), 0.5f);
        lA1 = sc (4453); lA2 = sc (3720); lB1 = sc (4217); lB2 = sc (3163);
        dA1.prepare (lA1 + 8); dA2.prepare (lA2 + 8); dB1.prepare (lB1 + 8); dB2.prepare (lB2 + 8);
        dampA.set (fs, 7000.0); dampB.set (fs, 7000.0);
        outHpL.set (fs, 110.0); outHpR.set (fs, 110.0);
        const double t[14] = { 266, 2974, 1913, 1996, 1990, 187, 1066, 353, 3627, 1228, 2673, 2111, 335, 121 };
        for (int i = 0; i < 14; ++i) tap[i] = juce::jmax (1, sc (t[i]));
        lfoInc = (float) (juce::MathConstants<double>::twoPi * 0.8 / fs);
        exc = (float) (6.0 * fs / 29761.0);
        clear();
    }
    void clear()
    {
        pre.clear(); bw.clear(); for (auto& a : inAp) a.clear();
        modA.clear(); modB.clear(); decA.clear(); decB.clear(); dA1.clear(); dA2.clear(); dB1.clear(); dB2.clear();
        dampA.clear(); dampB.clear(); outHpL.clear(); outHpR.clear(); fbA = fbB = 0.0f;
    }
    // seconds = RT60, size 0..1 (pre-delay and density), bright 0..1 (damping)
    void setParameters (float seconds, float size, float bright)
    {
        const double loop = (double) (sc (672) + lA1 + sc (1800) + lA2 + sc (908) + lB1 + sc (2656) + lB2) / fs;
        // decay is applied 4 times per trip round the tank
        decayTarget = (float) juce::jlimit (0.05, 0.97, std::pow (10.0, -3.0 * loop / (4.0 * std::max (0.3f, seconds))));
        preTarget = (float) ((0.004 + 0.05 * size) * fs);
        const double damp = 3500.0 + 10000.0 * bright;
        dampA.set (fs, damp); dampB.set (fs, damp);
        decA.g = decB.g = 0.35f + 0.25f * size;   // denser tail for bigger rooms
        // equal loudness whatever the decay: long tails are quieter per sample
        gain = 0.55f * std::sqrt (1.0f - decayTarget * decayTarget) / std::sqrt (1.0f - 0.5f * 0.5f);
    }
    inline void process (float inL, float inR, float& outL, float& outR)
    {
        decay += (decayTarget - decay) * 0.0005f;
        preLen += (preTarget - preLen) * 0.0005f;
        pre.push ((inL + inR) * 0.5f);
        float x = bw.lp (pre.hermite (1.0 + preLen));
        for (auto& a : inAp) x = a.tick (x);
        lfo += lfoInc; if (lfo > juce::MathConstants<float>::twoPi) lfo -= juce::MathConstants<float>::twoPi;
        const float m = std::sin (lfo) * exc, m2 = std::cos (lfo) * exc;

        float a = modA.tickMod (x + fbB * decay, m);
        dA1.push (a);
        a = dampA.lp (dA1.at (lA1)) * decay;
        a = decA.tick (a);
        dA2.push (a);
        float b = modB.tickMod (x + fbA * decay, m2);
        dB1.push (b);
        b = dampB.lp (dB1.at (lB1)) * decay;
        b = decB.tick (b);
        dB2.push (b);
        fbA = dA2.at (lA2); fbB = dB2.at (lB2);

        float l = dB1.at (tap[0]) + dB1.at (tap[1]) - decB.l.at (tap[2]) + dB2.at (tap[3]) - dA1.at (tap[4]) - decA.l.at (tap[5]) - dA2.at (tap[6]);
        float r = dA1.at (tap[7]) + dA1.at (tap[8]) - decA.l.at (tap[9]) + dA2.at (tap[10]) - dB1.at (tap[11]) - decB.l.at (tap[12]) - dB2.at (tap[13]);
        outL = outHpL.hp (l) * gain; outR = outHpR.hp (r) * gain;
    }
};

// --- Spiral (DJM-V10 beat FX): an echo whose repeats are smeared into a reverb, time change bends the pitch --
struct Spiral
{
    Line l[2]; Allpass ap[2][4]; OnePole lp[2], hp[2];
    double fs = 48000.0, cur = 4800.0, target = 4800.0, glide = 0.0001;
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        static const double t[2][4] = { { 0.0047, 0.0113, 0.0171, 0.0239 }, { 0.0053, 0.0127, 0.0181, 0.0251 } };
        for (int c = 0; c < 2; ++c)
        {
            l[c].prepare ((int) (2.5 * fs));
            for (int i = 0; i < 4; ++i) ap[c][i].prepare ((int) (t[c][i] * fs), 0.62f);
            lp[c].set (fs, 6500.0); hp[c].set (fs, 120.0);
        }
        glide = 1.0 - std::exp (-1.0 / (0.12 * fs));
        cur = target;
    }
    void clear() { for (int c = 0; c < 2; ++c) { l[c].clear(); lp[c].clear(); hp[c].clear(); for (auto& a : ap[c]) a.clear(); } }
    void setTime (double samples) { target = juce::jlimit (8.0, (double) l[0].size - 8.0, samples); }
    inline void process (float inL, float inR, float fb, float& outL, float& outR)
    {
        cur += (target - cur) * glide;
        float y[2] = { l[0].hermite (cur), l[1].hermite (cur) };
        for (int c = 0; c < 2; ++c)
        {
            float v = hp[c].hp (lp[c].lp (y[c]));
            for (auto& a : ap[c]) v = a.tick (v);   // each pass is smeared a bit more: the echo turns into a reverb
            l[c].push (safe ((c ? inR : inL) + v * fb));
        }
        outL = y[0]; outR = y[1];
    }
};

// --- octave-up pitch shifter (two crossfaded read heads) -----------------------------------------------
struct OctaveUp
{
    Line l; double fs = 48000.0; float p = 0.0f, inc = 0.0f, win = 2400.0f;
    void prepare (double sampleRate) { fs = sampleRate; win = (float) (0.06 * fs); l.prepare ((int) win * 2 + 16); inc = 1.0f / win; p = 0.0f; }
    void clear() { l.clear(); }
    inline float tick (float x)
    {
        l.push (x);
        p += inc; if (p >= 1.0f) p -= 1.0f;   // ratio 2: the head moves one extra sample per sample
        float q = p + 0.5f; if (q >= 1.0f) q -= 1.0f;
        const float d1 = 2.0f + (1.0f - p) * win, d2 = 2.0f + (1.0f - q) * win;
        const float g1 = std::sin (juce::MathConstants<float>::pi * p), g2 = std::sin (juce::MathConstants<float>::pi * q);
        return l.hermite (d1) * g1 + l.hermite (d2) * g2;
    }
};

// --- Shimmer: plate reverb whose tail is pitched up an octave and fed back in --------------------------------
struct Shimmer
{
    Plate plate; OctaveUp up[2]; OnePole hp[2], lp[2];
    float fbL = 0.0f, fbR = 0.0f;
    void prepare (double fs)
    {
        plate.prepare (fs);
        for (int c = 0; c < 2; ++c) { up[c].prepare (fs); hp[c].set (fs, 400.0); lp[c].set (fs, 9000.0); }
    }
    void clear() { plate.clear(); for (int c = 0; c < 2; ++c) { up[c].clear(); hp[c].clear(); lp[c].clear(); } fbL = fbR = 0.0f; }
    void setSize (float size) { plate.setParameters (2.0f + 10.0f * size, 0.4f + 0.6f * size, 0.75f); }
    inline void process (float inL, float inR, float shimmer, float& outL, float& outR)
    {
        float l, r;
        plate.process (inL + fbL, inR + fbR, l, r);
        fbL = lp[0].lp (hp[0].hp (up[0].tick (l))) * shimmer;
        fbR = lp[1].lp (hp[1].hp (up[1].tick (r))) * shimmer;
        outL = l; outR = r;
    }
};

// --- Vinyl brake: the sound slows down to a stop (turntable stop), back at once on release ---------------------
struct VinylBrake
{
    Line l[2]; double readPos = 0.0, speed = 1.0, decel = 0.0; bool braking = false; float back = 1.0f, backInc = 0.0f;
    void prepare (double fs) { for (auto& x : l) x.prepare ((int) (6.0 * fs)); backInc = (float) (1.0 / (0.012 * fs)); }
    void clear() { for (auto& x : l) x.clear(); }
    void engage (bool on, double fs, double stopSeconds)
    {
        if (on && ! braking) { braking = true; speed = 1.0; readPos = 1.0; decel = 1.0 / (stopSeconds * fs); }
        if (! on && braking) { braking = false; back = 0.0f; }
    }
    // processes in place; readPos = how far behind the live signal the head is
    inline void process (float& L, float& R)
    {
        l[0].push (L); l[1].push (R);
        if (braking)
        {
            speed = std::max (0.0, speed - decel);
            readPos += 1.0 - speed;   // the head falls behind as it slows
            const float g = (float) std::pow (speed, 0.35);
            L = speed > 0.0005 ? l[0].hermite (std::min (readPos, (double) l[0].size - 8.0)) * g : 0.0f;
            R = speed > 0.0005 ? l[1].hermite (std::min (readPos, (double) l[1].size - 8.0)) * g : 0.0f;
            lastL = L; lastR = R;
        }
        else if (back < 1.0f)   // short crossfade from the stopped sound back to live
        {
            L = lastL * (1.0f - back) + L * back; R = lastR * (1.0f - back) + R * back;
            back = std::min (1.0f, back + backInc);
        }
    }
    float lastL = 0.0f, lastR = 0.0f;
};
}
