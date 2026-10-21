#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include "../Station/StationDSP.h"

// Effects of Brume Pad (program 1), each driven by ONE knob: 0 = off, turning up = more effect.
namespace pad
{
using station::Svf;
using station::Rng;

// Stereo delay line; a change of delay time crossfades between two read heads (no pitch glide)
struct XfadeDelay
{
    std::vector<float> bufL, bufR;
    int pos = 0, size = 1;
    double cur = 1000.0, prev = 1000.0;
    float fade = 1.0f, fadeStep = 0.001f;

    void prepare (double fs, double maxSeconds)
    {
        size = (int) (fs * maxSeconds) + 4;
        bufL.assign ((size_t) size, 0.0f); bufR.assign ((size_t) size, 0.0f);
        pos = 0; fade = 1.0f; fadeStep = (float) (1.0 / (0.05 * fs));
    }
    void clear() { std::fill (bufL.begin(), bufL.end(), 0.0f); std::fill (bufR.begin(), bufR.end(), 0.0f); }
    void setDelay (double samples)
    {
        samples = juce::jlimit (2.0, (double) size - 3.0, samples);
        if (std::abs (samples - cur) < 0.5) return;
        prev = fade < 1.0f ? prev : cur;
        cur = samples; fade = 0.0f;
    }
    inline float tap (const std::vector<float>& b, double d) const
    {
        double rp = (double) pos - d;
        while (rp < 0.0) rp += size;
        const int i0 = (int) rp; const int i1 = (i0 + 1) % size;
        const float fr = (float) (rp - i0);
        return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * fr;
    }
    inline void read (float& l, float& r)
    {
        l = tap (bufL, cur); r = tap (bufR, cur);
        if (fade < 1.0f)
        {
            const float g = fade;
            l = l * g + tap (bufL, prev) * (1.0f - g);
            r = r * g + tap (bufR, prev) * (1.0f - g);
            fade = std::min (1.0f, fade + fadeStep);
        }
    }
    inline void write (float l, float r)
    {
        bufL[(size_t) pos] = l; bufR[(size_t) pos] = r;
        if (++pos >= size) pos = 0;
    }
};

// Bit / sample-rate crusher
struct Crusher
{
    float holdL = 0, holdR = 0, phase = 0;
    inline void process (float& l, float& r, float amount)   // amount 0..1
    {
        if (amount <= 0.001f) return;
        const float bits = 16.0f - 12.0f * amount;               // 16 -> 4 bits
        const float step = std::pow (2.0f, 1.0f - bits);
        const float rate = 1.0f - 0.92f * amount;                 // keep 1 sample in N
        phase += rate;
        if (phase >= 1.0f) { phase -= 1.0f; holdL = l; holdR = r; }
        l = std::round (holdL / step) * step;
        r = std::round (holdR / step) * step;
    }
};

// One echo with a coloured feedback path: 0 = high-pass echo, 1 = low-pass echo, 2 = crush echo, 3 = clean delay
struct Echo
{
    XfadeDelay line;
    Svf fbL, fbR;
    Crusher crush;
    int kind = 3;
    float fs = 44100.0f;

    void prepare (double sampleRate, int k) { fs = (float) sampleRate; kind = k; line.prepare (sampleRate, 4.0); }
    void clear() { line.clear(); fbL.reset(); fbR.reset(); }

    // filterHz: feedback filter cutoff; fb: feedback amount; returns the wet signal
    inline void process (float inL, float inR, float fb, float colour, float& outL, float& outR)
    {
        float yl, yr;
        line.read (yl, yr);
        float fl = yl, fr = yr;
        switch (kind)
        {
            case 0: fl = fbL.tick (fl).hp; fr = fbR.tick (fr).hp; break;
            case 1: fl = fbL.tick (fl).lp; fr = fbR.tick (fr).lp; break;
            case 2: crush.process (fl, fr, colour); fl = fbL.tick (fl).lp; fr = fbR.tick (fr).lp; break;
            default: fl = fbL.tick (fl).lp; fr = fbR.tick (fr).lp; break;
        }
        // soft limit in the loop so high feedback never runs away
        line.write (inL + std::tanh (fl * fb), inR + std::tanh (fr * fb));
        outL = yl; outR = yr;
    }
    void setFilter (float hz, float q = 0.7f) { fbL.set (fs, hz, q); fbR.set (fs, hz, q); }
};

// Smoothed one-knob value with KILL: after a kill the effect stays off until the knob is moved again
struct FxKnob
{
    float target = 0.0f, value = 0.0f, killedAt = -1.0f;
    bool killed = false;
    void set (float v)
    {
        if (killed && std::abs (v - killedAt) < 0.03f) { target = v; return; }
        killed = false; target = v;
    }
    void kill() { killed = true; killedAt = target; }
    float effective() const { return killed ? 0.0f : target; }
};

// Noise voiced like the Teile Revolo (measured): flat 170 Hz - 8 kHz, gentle roll-off above 10 kHz,
// almost independent left and right channels
struct RevoloNoise
{
    Rng rng;
    Svf hpL, hpR, lpL, lpR;
    void prepare (float fs)
    {
        hpL.set (fs, 130.0f, 0.707f); hpR.set (fs, 130.0f, 0.707f);
        lpL.set (fs, 13500.0f, 0.707f); lpR.set (fs, 13500.0f, 0.707f);
        rng.s = 0xbeef1234u;
    }
    inline void tick (float& l, float& r)
    {
        const float c = rng.next();
        const float a = c * 0.38f + rng.next() * 0.92f, b = c * 0.38f + rng.next() * 0.92f;
        l = lpL.tick (hpL.tick (a * 0.5f).hp).lp;
        r = lpR.tick (hpR.tick (b * 0.5f).hp).lp;
    }
};
}
