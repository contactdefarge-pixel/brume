#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include "../Station/StationDSP.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "BinaryData.h"

// Effects of Hydra (program 1), each driven by ONE knob: 0 = off, turning up = more effect.
namespace pad
{
using station::Svf;
using station::Rng;

// Stereo delay line; a change of delay time crossfades between two read heads (no pitch glide)
struct XfadeDelay
{
    std::vector<float> bufL, bufR;
    int pos = 0, size = 1;
    double cur = 1000.0, prev = 1000.0, target = 1000.0, glideCoef = 0.0001;
    float fade = 1.0f, fadeStep = 0.001f;
    bool glide = false;   // true: a change of time slides the read head (tape-style pitch bend, like the DJM-V10 delays)

    void prepare (double fs, double maxSeconds)
    {
        size = (int) (fs * maxSeconds) + 4;
        bufL.assign ((size_t) size, 0.0f); bufR.assign ((size_t) size, 0.0f);
        pos = 0; fade = 1.0f; fadeStep = (float) (1.0 / (0.05 * fs));
        glideCoef = 1.0 - std::exp (-1.0 / (0.16 * fs));
    }
    void clear() { std::fill (bufL.begin(), bufL.end(), 0.0f); std::fill (bufR.begin(), bufR.end(), 0.0f); }
    void setDelay (double samples)
    {
        samples = juce::jlimit (2.0, (double) size - 3.0, samples);
        if (glide) { target = samples; return; }
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
        if (glide) cur += (target - cur) * glideCoef;
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

// small allpass used to smear the dub echo repeats
struct Diffuser
{
    std::vector<float> b; int pos = 0; float g = 0.5f;
    void prepare (int len, float gain) { b.assign ((size_t) std::max (2, len), 0.0f); pos = 0; g = gain; }
    void clear() { std::fill (b.begin(), b.end(), 0.0f); }
    inline float tick (float x) { const float d = b[(size_t) pos]; const float v = x + g * d; b[(size_t) pos] = v; if (++pos >= (int) b.size()) pos = 0; return d - g * v; }
};

// Dense hall reverb for the REVERB send (replaces the pedal's FDN here):
// pre-delay -> 4 allpass diffusers per side -> 8-line feedback delay network (Householder matrix),
// slowly modulated lines (no metallic ringing), frequency-dependent decay, low end kept out of the tail.
struct HallReverb
{
    struct Line
    {
        std::vector<float> b; int w = 0;
        void prepare (int n) { b.assign ((size_t) std::max (8, n), 0.0f); w = 0; }
        void clear() { std::fill (b.begin(), b.end(), 0.0f); }
        inline float read (float d) const
        {
            float rp = (float) w - d;
            const int sz = (int) b.size();
            while (rp < 0.0f) rp += (float) sz;
            const int i0 = (int) rp, i1 = (i0 + 1) % sz;
            const float fr = rp - (float) i0;
            return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * fr;
        }
        inline void write (float x) { b[(size_t) w] = x; if (++w >= (int) b.size()) w = 0; }
    };
    static constexpr int N = 8;
    double fs = 48000.0;
    Line pre[2]; Diffuser dif[2][4]; Line line[N];
    float lenBase[N] {}, lenCur[N] {}, gain[N] {}, lp[N] {}, lfo[N] {}, lfoInc[N] {};
    float preCur = 0.0f, preTarget = 0.0f, dampCoef = 0.3f, modDepth = 4.0f, sizeScale = 1.0f;
    Svf inHp[2], inLp[2];

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const double k = fs / 48000.0;
        static const int base[N] = { 2203, 2621, 2953, 3307, 3691, 4013, 4447, 4801 };
        for (int i = 0; i < N; ++i)
        {
            lenBase[i] = (float) (base[i] * k);
            line[i].prepare ((int) (lenBase[i] * 1.3f + 64.0f));
            lenCur[i] = lenBase[i];
            lfo[i] = (float) i * 0.7f;
            lfoInc[i] = (float) (juce::MathConstants<double>::twoPi * (0.11 + 0.093 * i) / fs);
            lp[i] = 0.0f;
        }
        static const int dl[2][4] = { { 142, 379, 107, 277 }, { 151, 367, 113, 293 } };
        static const float dg[4] = { 0.75f, 0.75f, 0.625f, 0.625f };
        for (int c = 0; c < 2; ++c)
        {
            pre[c].prepare ((int) (0.12 * fs));
            for (int j = 0; j < 4; ++j) dif[c][j].prepare ((int) (dl[c][j] * fs / 29761.0), dg[j]);
            inHp[c].set ((float) fs, 120.0f, 0.6f);
            inLp[c].set ((float) fs, 11000.0f, 0.6f);
        }
        setParameters (2.0f, 0.5f, 0.6f);
    }
    void clear()
    {
        for (auto& l : line) l.clear();
        for (int c = 0; c < 2; ++c) { pre[c].clear(); for (auto& d : dif[c]) d.clear(); inHp[c].reset(); inLp[c].reset(); }
        for (auto& v : lp) v = 0.0f;
    }
    // seconds = RT60 of the tail, size 0..1 (room size, pre-delay, modulation), brightness 0..1
    void setParameters (float seconds, float size, float brightness)
    {
        sizeScale = 0.55f + 0.7f * size;
        preTarget = (float) ((0.006 + 0.034 * size) * fs);
        modDepth = (float) ((2.0 + 7.0 * size) * fs / 48000.0);
        const float cutoff = 2500.0f + 9000.0f * brightness;
        dampCoef = std::exp (-juce::MathConstants<float>::twoPi * cutoff / (float) fs);
        for (int i = 0; i < N; ++i)
        {
            const float len = lenBase[i] * sizeScale;
            gain[i] = std::pow (10.0f, -3.0f * len / (std::max (0.2f, seconds) * (float) fs));
        }
        for (int c = 0; c < 2; ++c) inLp[c].set ((float) fs, 6000.0f + 8000.0f * brightness, 0.6f);
    }
    void process (float* left, float* right, int n)
    {
        static const float sL[N] = { 1, -1, 1, -1, 1, -1, 1, -1 };
        static const float sR[N] = { 1, 1, -1, -1, 1, 1, -1, -1 };
        for (int s = 0; s < n; ++s)
        {
            preCur += (preTarget - preCur) * 0.0005f;
            float in[2] = { left[s], right[s] };
            for (int c = 0; c < 2; ++c)
            {
                float x = inLp[c].tick (inHp[c].tick (in[c]).hp).lp;
                pre[c].write (x);
                x = pre[c].read (preCur + 1.0f);
                for (auto& d : dif[c]) x = d.tick (x);
                in[c] = x;
            }
            float o[N], sum = 0.0f;
            for (int i = 0; i < N; ++i)
            {
                lenCur[i] += (lenBase[i] * sizeScale - lenCur[i]) * 0.0002f;
                lfo[i] += lfoInc[i]; if (lfo[i] > juce::MathConstants<float>::twoPi) lfo[i] -= juce::MathConstants<float>::twoPi;
                float y = line[i].read (lenCur[i] + modDepth * (1.0f + std::sin (lfo[i])));
                lp[i] = y + (lp[i] - y) * dampCoef;   // one-pole low-pass in the loop: highs decay faster
                o[i] = lp[i] * gain[i];
                sum += o[i];
            }
            const float h = sum * (2.0f / (float) N);
            float outL = 0.0f, outR = 0.0f;
            for (int i = 0; i < N; ++i)
            {
                const float fb = o[i] - h;   // Householder: lossless mixing of the 8 lines
                line[i].write (fb + ((i & 1) ? in[1] : in[0]) * 0.6f);
                outL += o[i] * sL[i]; outR += o[i] * sR[i];
            }
            left[s] = outL * 0.8f;
            right[s] = outR * 0.8f;
        }
    }
};

// One echo with a coloured feedback path: 0 = high-pass echo, 1 = low-pass echo, 2 = crush echo, 3 = clean delay,
// 4 = dub echo (dark, saturated, diffused repeats, like the DJM-V10 / 900NXS2 dub echo)
struct Echo
{
    XfadeDelay line;
    Svf fbL, fbR, dubHpL, dubHpR;
    Diffuser difL1, difL2, difR1, difR2;
    Crusher crush;
    int kind = 3;
    float fs = 44100.0f;

    void prepare (double sampleRate, int k)
    {
        fs = (float) sampleRate; kind = k; line.prepare (sampleRate, 4.0);
        difL1.prepare ((int) (0.0071 * sampleRate), 0.55f); difL2.prepare ((int) (0.0113 * sampleRate), 0.5f);
        difR1.prepare ((int) (0.0079 * sampleRate), 0.55f); difR2.prepare ((int) (0.0127 * sampleRate), 0.5f);
        dubHpL.set (fs, 180.0f, 0.7f); dubHpR.set (fs, 180.0f, 0.7f);
    }
    void clear() { line.clear(); fbL.reset(); fbR.reset(); difL1.clear(); difL2.clear(); difR1.clear(); difR2.clear(); }

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
            case 4:
                fl = difL2.tick (difL1.tick (dubHpL.tick (fbL.tick (fl).lp).hp));
                fr = difR2.tick (difR1.tick (dubHpR.tick (fbR.tick (fr).lp).hp));
                fl = std::tanh (fl * 1.4f) / 1.4f; fr = std::tanh (fr * 1.4f) / 1.4f;
                break;
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

// The four X-PAD sounds, extracted from a recording of the RMX-1000 (one clean hit each, mono 44.1 kHz),
// resampled to the session rate. 0 KICK, 1 SNARE, 2 CLAP, 3 HAT.
struct RmxKit
{
    std::array<std::vector<float>, 4> s;
    void prepare (double fs)
    {
        const void* data[4] = { BinaryData::rmx_kick_wav, BinaryData::rmx_snare_wav, BinaryData::rmx_clap_wav, BinaryData::rmx_hat_wav };
        const int size[4] = { BinaryData::rmx_kick_wavSize, BinaryData::rmx_snare_wavSize, BinaryData::rmx_clap_wavSize, BinaryData::rmx_hat_wavSize };
        juce::WavAudioFormat wav;
        for (int i = 0; i < 4; ++i)
        {
            std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (new juce::MemoryInputStream (data[i], (size_t) size[i], false), true));
            if (r == nullptr) { s[(size_t) i].assign (16, 0.0f); continue; }
            juce::AudioBuffer<float> src (1, (int) r->lengthInSamples);
            r->read (&src, 0, (int) r->lengthInSamples, 0, true, false);
            const double ratio = r->sampleRate / fs;
            const int outLen = (int) std::ceil (src.getNumSamples() / ratio);
            s[(size_t) i].assign ((size_t) outLen + 4, 0.0f);
            juce::LagrangeInterpolator interp;
            interp.process (ratio, src.getReadPointer (0), s[(size_t) i].data(), outLen);
        }
    }
};

// ---------------------------------------------------------------------------------------------
// Pattern Player (program 3): 3 kits of 8 one-shots, like Traktor's Pattern Player.
// 808 and 909 are synthesised in the style of those machines; RMX = the 4 X-PAD samples + 4 909 extras.
namespace pp
{
    constexpr int numKits = 3, soundsPerKit = 8, numPatterns = 8;
    inline const char* kitName (int k) { static const char* n[] = { "808", "909", "RMX" }; return n[juce::jlimit (0, numKits - 1, k)]; }
    inline const char* soundName (int s) { static const char* n[] = { "KICK", "SNARE", "CLAP", "CL HAT", "OP HAT", "RIM", "TOM", "CYMBAL" }; return n[juce::jlimit (0, 7, s)]; }
    // 8 starting patterns (bit n = step n+1); the steps can then be edited on the pads
    inline int patternMask (int p)
    {
        auto m = [] (std::initializer_list<int> st) { int x = 0; for (int i : st) x |= 1 << i; return x; };
        static const int pats[numPatterns] = {
            m ({ 0, 4, 8, 12 }),                 // 1 four on the floor
            m ({ 2, 6, 10, 14 }),                // 2 offbeats
            m ({ 4, 12 }),                       // 3 backbeat (2 and 4)
            m ({ 0, 2, 4, 6, 8, 10, 12, 14 }),   // 4 eighths
            0xffff,                              // 5 sixteenths
            m ({ 0, 3, 6, 10, 12 }),             // 6 syncopated
            m ({ 2, 3, 6, 7, 10, 11, 14, 15 }),  // 7 shuffle pairs
            m ({ 0, 8, 12, 13, 14, 15 }) };      // 8 fill
        return pats[juce::jlimit (0, numPatterns - 1, p)];
    }

    struct Kits
    {
        std::array<std::array<std::vector<float>, soundsPerKit>, numKits> s;
        float fs = 44100.0f;

        static void normalise (std::vector<float>& b, float peak)
        {
            float mx = 1e-9f; for (float v : b) mx = std::max (mx, std::abs (v));
            for (auto& v : b) v *= peak / mx;
            const int f = std::min ((int) b.size(), 64);
            for (int i = 0; i < f; ++i) b[b.size() - 1 - (size_t) i] *= (float) i / (float) f;   // click-free end
        }
        std::vector<float> make (float seconds) const { return std::vector<float> ((size_t) (seconds * fs), 0.0f); }

        // 808 metallic source: six square waves (the classic hi-hat / cymbal recipe)
        float metal (float t) const
        {
            static const float f[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
            float y = 0.0f; for (float hz : f) y += std::fmod (t * hz * 1.0f, 1.0f) < 0.5f ? 1.0f : -1.0f;
            return y / 6.0f;
        }

        void prepare (double sampleRate, const std::array<std::vector<float>, 4>& rmx)
        {
            fs = (float) sampleRate;
            const float tp = 6.28318530718f;
            Rng rng; rng.s = 0x808u;
            // ---- 808 ----
            { auto b = make (1.2f); float ph = 0; for (size_t i = 0; i < b.size(); ++i) { const float t = (float) i / fs; ph += (49.0f + 60.0f * std::exp (-t / 0.02f)) / fs; b[i] = std::sin (tp * ph) * std::exp (-t / 0.45f); } normalise (b, 0.95f); s[0][0] = b; }
            { auto b = make (0.4f); Svf hp; hp.set (fs, 1800.0f, 0.7f); float p1 = 0, p2 = 0;
              for (size_t i = 0; i < b.size(); ++i) { const float t = (float) i / fs; p1 += 180.0f / fs; p2 += 330.0f / fs;
                b[i] = (std::sin (tp * p1) * 0.6f + std::sin (tp * p2) * 0.4f) * std::exp (-t / 0.06f) + hp.tick (rng.next()).hp * std::exp (-t / 0.11f) * 0.8f; } normalise (b, 0.9f); s[0][1] = b; }
            { auto b = make (0.5f); Svf bp; bp.set (fs, 1100.0f, 1.5f);
              for (size_t i = 0; i < b.size(); ++i) { const float t = (float) i / fs; float e = 0; for (int k = 0; k < 4; ++k) { const float tt = t - 0.01f * k; if (tt >= 0) e = std::max (e, std::exp (-tt / 0.005f)); }
                if (t > 0.03f) e = std::max (e, 0.6f * std::exp (-(t - 0.03f) / 0.18f)); b[i] = bp.tick (rng.next()).bp * e; } normalise (b, 0.9f); s[0][2] = b; }
            for (int oh = 0; oh < 2; ++oh)
            { auto b = make (oh ? 0.8f : 0.15f); Svf bp, hp; bp.set (fs, 8000.0f, 1.2f); hp.set (fs, 6500.0f, 0.7f);
              for (size_t i = 0; i < b.size(); ++i) { const float t = (float) i / fs; b[i] = hp.tick (bp.tick (metal (t)).bp).hp * std::exp (-t / (oh ? 0.32f : 0.035f)); } normalise (b, 0.7f); s[0][3 + oh] = b; }
            { auto b = make (0.12f); float ph = 0; for (size_t i = 0; i < b.size(); ++i) { const float t = (float) i / fs; ph += 1700.0f / fs; b[i] = (std::sin (tp * ph) + 0.5f * std::sin (tp * ph * 0.3f)) * std::exp (-t / 0.012f); } normalise (b, 0.8f); s[0][5] = b; }
            { auto b = make (0.9f); float ph = 0; for (size_t i = 0; i < b.size(); ++i) { const float t = (float) i / fs; ph += (110.0f + 25.0f * std::exp (-t / 0.05f)) / fs; b[i] = std::sin (tp * ph) * std::exp (-t / 0.3f); } normalise (b, 0.9f); s[0][6] = b; }
            { auto b = make (2.0f); Svf bp; bp.set (fs, 7500.0f, 0.8f); for (size_t i = 0; i < b.size(); ++i) { const float t = (float) i / fs; b[i] = bp.tick (metal (t * 1.3f) + rng.next() * 0.3f).bp * std::exp (-t / 0.7f); } normalise (b, 0.65f); s[0][7] = b; }
            // ---- 909 ----
            { auto b = make (0.6f); float ph = 0; for (size_t i = 0; i < b.size(); ++i) { const float t = (float) i / fs; ph += (54.0f + 180.0f * std::exp (-t / 0.025f)) / fs;
                float y = std::sin (tp * ph) * std::exp (-t / 0.28f) + rng.next() * std::exp (-t / 0.002f) * 0.6f; b[i] = std::tanh (2.2f * y); } normalise (b, 0.97f); s[1][0] = b; }
            { auto b = make (0.45f); Svf hp, bp; hp.set (fs, 2500.0f, 0.7f); bp.set (fs, 5500.0f, 0.9f); float ph = 0;
              for (size_t i = 0; i < b.size(); ++i) { const float t = (float) i / fs; ph += (190.0f + 80.0f * std::exp (-t / 0.01f)) / fs; const float n = rng.next();
                b[i] = std::sin (tp * ph) * std::exp (-t / 0.05f) * 0.8f + (hp.tick (n).hp * 0.6f + bp.tick (n).bp * 1.2f) * std::exp (-t / 0.16f); } normalise (b, 0.92f); s[1][1] = b; }
            { auto b = make (0.5f); Svf bp, hp; bp.set (fs, 1350.0f, 1.2f); hp.set (fs, 800.0f, 0.7f);
              for (size_t i = 0; i < b.size(); ++i) { const float t = (float) i / fs; float e = 0; for (int k = 0; k < 4; ++k) { const float tt = t - 0.0095f * k; if (tt >= 0) e = std::max (e, std::exp (-tt / 0.0045f)); }
                if (t > 0.029f) e = std::max (e, 0.65f * std::exp (-(t - 0.029f) / 0.14f)); const float n = rng.next(); b[i] = (bp.tick (n).bp * 2.0f + hp.tick (n).hp * 0.4f) * e; } normalise (b, 0.92f); s[1][2] = b; }
            for (int oh = 0; oh < 2; ++oh)
            { auto b = make (oh ? 0.7f : 0.12f); Svf hp, bp; hp.set (fs, 7500.0f, 0.7f); bp.set (fs, 10500.0f, 1.4f);
              for (size_t i = 0; i < b.size(); ++i) { const float t = (float) i / fs; const float n = rng.next(); b[i] = (hp.tick (n).hp * 0.8f + bp.tick (n).bp) * std::exp (-t / (oh ? 0.28f : 0.03f)); } normalise (b, 0.7f); s[1][3 + oh] = b; }
            { auto b = make (0.1f); Svf bp; bp.set (fs, 2000.0f, 3.0f); for (size_t i = 0; i < b.size(); ++i) { const float t = (float) i / fs; b[i] = bp.tick (i < 40 ? 1.0f : 0.0f).bp * std::exp (-t / 0.015f); } normalise (b, 0.85f); s[1][5] = b; }
            { auto b = make (0.6f); float ph = 0; for (size_t i = 0; i < b.size(); ++i) { const float t = (float) i / fs; ph += (150.0f + 60.0f * std::exp (-t / 0.03f)) / fs; b[i] = std::tanh (1.5f * std::sin (tp * ph)) * std::exp (-t / 0.18f) + rng.next() * std::exp (-t / 0.003f) * 0.3f; } normalise (b, 0.9f); s[1][6] = b; }
            { auto b = make (2.2f); Svf hp, bp; hp.set (fs, 5000.0f, 0.7f); bp.set (fs, 9000.0f, 2.0f); for (size_t i = 0; i < b.size(); ++i) { const float t = (float) i / fs; const float n = rng.next(); b[i] = (hp.tick (n).hp * 0.5f + bp.tick (n).bp) * std::exp (-t / 0.9f) * std::min (1.0f, t / 0.001f); } normalise (b, 0.6f); s[1][7] = b; }
            // ---- RMX: the X-PAD samples + 909 extras ----
            s[2][0] = rmx[0]; s[2][1] = rmx[1]; s[2][2] = rmx[2]; s[2][3] = rmx[3];
            s[2][4] = s[1][4]; s[2][5] = s[1][5]; s[2][6] = s[1][6]; s[2][7] = s[1][7];
        }
    };
}
}
