#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

// Sound sources of Brume Station: everything is synthesised (no samples, no licensing issue).
namespace station
{
constexpr float kTwoPi = 6.28318530718f;

struct Rng
{
    juce::uint32 s = 0x9e3779b9u;
    inline float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s >> 8) * (1.0f / 8388608.0f) * 2.0f - 1.0f; }
};

// Zero-delay state-variable filter (Simper), one channel
struct Svf
{
    float ic1 = 0, ic2 = 0, g = 0.1f, k = 1.4f, a1 = 0, a2 = 0, a3 = 0;
    void set (float fs, float hz, float q)
    {
        hz = juce::jlimit (16.0f, fs * 0.45f, hz);
        g = std::tan (juce::MathConstants<float>::pi * hz / fs);
        k = 1.0f / std::max (0.3f, q);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    struct Out { float lp, bp, hp; };
    inline Out tick (float x)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        return { v2, v1, x - k * v1 - v2 };
    }
};

//==============================================================================
// Drum kit for the pattern player: 0 kick, 1 clap, 2 closed hat, 3 open hat, 4 perc
struct DrumVoice
{
    int type = 0;
    bool active = false;
    float t = 0, ph = 0, vel = 1, fs = 44100, kit = 0.5f;
    Svf f1, f2;
    Rng rng;

    void trigger (float sampleRate, int drum, float velocity, float kitAmt)
    {
        fs = sampleRate; type = drum; vel = velocity; kit = kitAmt; t = 0; ph = 0; active = true;
        f1.reset(); f2.reset();
        switch (type)
        {
            case 1: f1.set (fs, 1150.0f + 500.0f * kit, 1.4f); f2.set (fs, 600.0f, 0.7f); break;
            case 2: f1.set (fs, 6500.0f + 3000.0f * kit, 0.7f); f2.set (fs, 10500.0f, 1.2f); break;
            case 3: f1.set (fs, 6000.0f + 2500.0f * kit, 0.7f); f2.set (fs, 9000.0f, 1.0f); break;
            default: break;
        }
    }

    inline float tick()
    {
        if (! active) return 0.0f;
        float y = 0.0f, end = 1.0f;
        const float dt = 1.0f / fs;
        switch (type)
        {
            case 0:   // kick: sine with a fast pitch drop, a click, gentle saturation
            {
                const float dec = 0.28f + 0.35f * (1.0f - kit);
                const float f = 42.0f + 10.0f * kit + 120.0f * std::exp (-t / 0.032f);
                ph += f * dt; if (ph >= 1.0f) ph -= 1.0f;
                y = std::sin (kTwoPi * ph) * std::exp (-t / dec);
                y += rng.next() * std::exp (-t / 0.0015f) * 0.35f;
                y = std::tanh (1.6f * y) * 0.95f;
                end = dec * 7.0f;
                break;
            }
            case 1:   // clap: three short noise bursts, then a tail
            {
                const float n = rng.next();
                const float b = f1.tick (n).bp * 2.2f;
                float env = 0.0f;
                for (int i = 0; i < 3; ++i)
                {
                    const float tt = t - 0.011f * (float) i;
                    if (tt >= 0.0f) env = std::max (env, std::exp (-tt / 0.0055f));
                }
                env = std::max (env, 0.55f * std::exp (-std::max (0.0f, t - 0.022f) / (0.12f + 0.08f * kit)) * (t > 0.022f ? 1.0f : 0.0f));
                y = b * env;
                end = 0.9f;
                break;
            }
            case 2: case 3:   // hats: high-passed noise with a metallic band
            {
                const float n = rng.next();
                y = (f1.tick (n).hp * 0.7f + f2.tick (n).bp * 0.9f);
                const float dec = type == 2 ? 0.035f + 0.02f * kit : 0.22f + 0.15f * kit;
                y *= std::exp (-t / dec) * (type == 2 ? 0.75f : 0.6f);
                end = dec * 7.0f;
                break;
            }
            default:  // perc: woody tom / conga
            {
                const float f = (230.0f + 160.0f * kit) * (1.0f + 0.35f * std::exp (-t / 0.02f));
                ph += f * dt; if (ph >= 1.0f) ph -= 1.0f;
                y = (std::sin (kTwoPi * ph) + 0.25f * std::sin (kTwoPi * ph * 2.7f)) * std::exp (-t / (0.11f + 0.08f * kit)) * 0.75f;
                y += rng.next() * std::exp (-t / 0.002f) * 0.2f;
                end = 1.0f;
                break;
            }
        }
        t += dt;
        if (t > end) active = false;
        return y * vel;
    }
};

//==============================================================================
// One-shots: 0 impact, 1 crash, 2 clap (big), 3 swell (to a bar line), 4 riser (long, to a bar line), 5 downlifter, 6 sub drop
struct OneShot
{
    int type = 0;
    bool active = false;
    float t = 0, ph = 0, len = 1, fs = 44100;
    Svf fa, fb;
    Rng rng;

    void trigger (float sampleRate, int kind, float lengthSeconds = 1.0f)
    {
        fs = sampleRate; type = kind; t = 0; ph = 0; len = std::max (0.05f, lengthSeconds); active = true;
        fa.reset(); fb.reset();
        if (type == 1) { fa.set (fs, 5200.0f, 0.6f); fb.set (fs, 8200.0f, 2.5f); }
        if (type == 2) { fa.set (fs, 1300.0f, 1.2f); }
    }

    // returns mono sample
    inline float tick()
    {
        if (! active) return 0.0f;
        const float dt = 1.0f / fs;
        float y = 0.0f, end = 3.0f;
        switch (type)
        {
            case 0:   // impact: sub drop + noise thump
            {
                const float f = 31.0f + 34.0f * std::exp (-t / 0.35f);
                ph += f * dt; if (ph >= 1.0f) ph -= 1.0f;
                y = std::sin (kTwoPi * ph) * std::exp (-t / 1.1f) * 1.1f;
                fa.set (fs, 300.0f + 2500.0f * std::exp (-t / 0.08f), 0.7f);
                y += fa.tick (rng.next()).lp * std::exp (-t / 0.18f) * 1.2f;
                y = std::tanh (y * 1.2f);
                end = 5.0f;
                break;
            }
            case 1:   // crash: bright noise wash
            {
                const float n = rng.next();
                y = (fa.tick (n).hp * 0.6f + fb.tick (n).bp * 0.8f) * std::exp (-t / 0.9f);
                y *= std::min (1.0f, t / 0.002f);
                end = 6.0f;
                break;
            }
            case 2:   // big clap
            {
                const float b = fa.tick (rng.next()).bp * 2.4f;
                float env = 0.0f;
                for (int i = 0; i < 4; ++i)
                {
                    const float tt = t - 0.012f * (float) i;
                    if (tt >= 0.0f) env = std::max (env, std::exp (-tt / 0.006f));
                }
                if (t > 0.036f) env = std::max (env, 0.6f * std::exp (-(t - 0.036f) / 0.3f));
                y = b * env;
                end = 2.0f;
                break;
            }
            case 4:   // riser: band-passed noise climbing, plus a rising tone, stops dead on the bar line
            {
                const float p = std::min (1.0f, t / len);
                fa.set (fs, 400.0f * std::pow (25.0f, p), 2.5f);
                const float f = 110.0f * std::pow (8.0f, p * p);
                ph += f * dt; if (ph >= 1.0f) ph -= 1.0f;
                y = (fa.tick (rng.next()).bp * 1.6f + 0.25f * std::sin (kTwoPi * ph)) * (0.15f + 0.85f * p * p);
                end = len;
                break;
            }
            case 5:   // downlifter: noise falling and fading
            {
                const float p = std::min (1.0f, t / len);
                fa.set (fs, 9000.0f * std::pow (0.03f, p), 1.2f);
                y = fa.tick (rng.next()).lp * (1.0f - p) * (1.0f - p) * 1.4f;
                end = len;
                break;
            }
            case 6:   // sub drop: long sine falling an octave
            {
                const float f = 62.0f * std::pow (0.5f, std::min (1.0f, t / 1.6f));
                ph += f * dt; if (ph >= 1.0f) ph -= 1.0f;
                y = std::tanh (1.4f * std::sin (kTwoPi * ph)) * std::exp (-t / 1.4f) * std::min (1.0f, t / 0.004f);
                end = 6.0f;
                break;
            }
            default:  // swell: noise opening up, louder and brighter until it stops dead on the bar line
            {
                const float p = std::min (1.0f, t / len);
                fa.set (fs, 250.0f * std::pow (40.0f, p), 0.9f);
                y = fa.tick (rng.next()).lp * p * p * 1.3f;
                end = len;
                break;
            }
        }
        t += dt;
        if (t >= end) active = false;
        return y;
    }
};

//==============================================================================
// Chord pad: 4 voices (bass + triad), 3 detuned saws each, gliding between chords, low-pass "opening".
struct PadSynth
{
    static constexpr int numVoices = 4, sawsPerVoice = 3;
    float fs = 44100;
    std::array<float, numVoices> curHz {}, targetHz {};
    std::array<std::array<float, sawsPerVoice>, numVoices> phase {};
    std::array<float, numVoices> sinePh {};
    Svf lpL, lpR;
    float cutoff = 800, gain = 0, lfo = 0;
    bool started = false;

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        Rng r;
        for (auto& v : phase) for (auto& p : v) p = (r.next() + 1.0f) * 0.5f;
    }

    // midi notes for the 4 voices
    void setChord (const std::array<int, numVoices>& notes)
    {
        for (int i = 0; i < numVoices; ++i)
        {
            targetHz[(size_t) i] = 440.0f * std::pow (2.0f, ((float) notes[(size_t) i] - 69.0f) / 12.0f);
            if (! started) curHz[(size_t) i] = targetHz[(size_t) i];
        }
        started = true;
    }

    static inline float polyBlep (float t, float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    // gainTarget 0..1 (fade), open 0..1 (filter + level), timbre 0..1 (warm -> airy)
    inline void tick (float gainTarget, float fadeCoef, float open, float timbre, float width, float& outL, float& outR)
    {
        gain += (gainTarget - gain) * fadeCoef;
        if (gain < 1.0e-5f && gainTarget <= 0.0f) { outL = outR = 0.0f; return; }

        lfo += 0.13f / fs; if (lfo >= 1.0f) lfo -= 1.0f;
        const float wob = std::sin (kTwoPi * lfo);
        const float glide = 1.0f - std::exp (-1.0f / (0.12f * fs));
        const float detune = 0.004f + 0.012f * timbre + 0.002f * wob;
        static const float spread[sawsPerVoice] = { -1.0f, 0.0f, 1.0f };
        float l = 0.0f, r = 0.0f;
        for (int v = 0; v < numVoices; ++v)
        {
            curHz[(size_t) v] *= std::pow (targetHz[(size_t) v] / curHz[(size_t) v], glide);
            const float vGain = v == 0 ? 0.55f : 1.0f;
            for (int s = 0; s < sawsPerVoice; ++s)
            {
                const float hz = curHz[(size_t) v] * (1.0f + spread[s] * detune);
                const float dt = hz / fs;
                float& p = phase[(size_t) v][(size_t) s];
                p += dt; if (p >= 1.0f) p -= 1.0f;
                const float saw = (2.0f * p - 1.0f) - polyBlep (p, dt);
                const float pan = 0.5f + 0.48f * width * spread[s] * (v % 2 == 0 ? 1.0f : -1.0f);
                l += saw * vGain * (1.0f - pan);
                r += saw * vGain * pan;
            }
            float& sp = sinePh[(size_t) v];
            sp += curHz[(size_t) v] * (v == 0 ? 0.5f : 1.0f) / fs; if (sp >= 1.0f) sp -= 1.0f;
            const float sine = std::sin (kTwoPi * sp) * (v == 0 ? 1.4f : 0.6f * (1.0f - timbre));
            l += sine; r += sine;
        }
        const float target = 140.0f * std::pow (2.0f, open * 6.2f + timbre * 0.8f) * (1.0f + 0.08f * wob);
        cutoff += (target - cutoff) * 0.002f;
        lpL.set (fs, cutoff, 0.85f); lpR.set (fs, cutoff, 0.85f);
        const float level = gain * (0.35f + 0.65f * open) * 0.14f;
        outL = lpL.tick (l).lp * level;
        outR = lpR.tick (r).lp * level;
    }
};

//==============================================================================
// Keys (Camelot wheel): index 0..11 = 1A..12A (minor), 12..23 = 1B..12B (major)
inline int keyRoot (int key)
{
    const int n = key % 12;
    return key < 12 ? (8 + 7 * n) % 12 : (11 + 7 * n) % 12;
}
inline bool keyIsMajor (int key) { return key >= 12; }
inline juce::String keyName (int key)
{
    static const char* notes[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return juce::String (key % 12 + 1) + (key < 12 ? "A" : "B") + "  " + notes[keyRoot (key)] + (key < 12 ? "m" : "");
}
inline const char* degreeName (int degree, bool major)
{
    static const char* maj[] = { "I", "ii", "iii", "IV", "V", "vi", "vii" };
    static const char* min[] = { "i", "ii", "III", "iv", "v", "VI", "VII" };
    return (major ? maj : min)[juce::jlimit (0, 6, degree)];
}
// bass note + triad folded into a fixed range, so chords move smoothly.
// degree 0..6 = I..VII, 7 = I an octave up; octave -1..1; sus replaces the third by the second (sus2)
inline std::array<int, 4> chordNotes (int key, int degree, int octave = 0, bool sus = false)
{
    static const int majS[] = { 0, 2, 4, 5, 7, 9, 11 }, minS[] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* sc = keyIsMajor (key) ? majS : minS;
    const int root = keyRoot (key);
    const int up = degree >= 7 ? 12 : 0;
    const int d0 = degree % 7;
    auto deg = [&] (int d) { return root + sc[d % 7] + 12 * (d / 7); };
    std::array<int, 4> n {};
    n[0] = 36 + (deg (d0) % 12) + 12 * octave;
    const int tones[3] = { 0, sus ? 1 : 2, 4 };
    int lo = 55 + up + 12 * octave;
    for (int i = 0; i < 3; ++i)
    {
        int m = deg (d0 + tones[i]);
        while (m < lo) m += 12;
        while (m >= lo + 12) m -= 12;
        n[(size_t) i + 1] = m;
    }
    return n;
}

//==============================================================================
// Punchy one-shot kit, rendered once into buffers (like the RMX-1000's X-PAD sounds):
// 0 snare, 1 clap, 2 kick, 3 closed hat, 4 open hat. Played back pitched (speed + pitch together).
struct Kit
{
    static constexpr int numSounds = 5;
    std::array<std::vector<float>, numSounds> s;
    float fs = 44100, snareTone = -1;

    static void normalise (std::vector<float>& b, float peak)
    {
        float m = 1e-9f; for (float v : b) m = std::max (m, std::abs (v));
        for (auto& v : b) v *= peak / m;
    }

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        for (int i = 0; i < numSounds; ++i) s[(size_t) i].assign ((size_t) (fs * (i == 4 ? 1.2f : 0.7f)), 0.0f);
        renderClap(); renderKick(); renderHat (s[3], 0.05f); renderHat (s[4], 0.32f);
        snareTone = -1; renderSnare (0.5f);
    }

    // tone 0 = fat and short, 1 = bright and long. Allocation-free (buffer prepared), safe on the audio thread.
    void renderSnare (float tone)
    {
        if (std::abs (tone - snareTone) < 0.01f) return;
        snareTone = tone;
        auto& b = s[0];
        Rng rng; rng.s = 0x1234567u;
        Svf hpN, bpN, hpC;
        hpN.set (fs, 1500.0f, 0.7f); bpN.set (fs, 3800.0f + 2400.0f * tone, 0.8f); hpC.set (fs, 4000.0f, 0.7f);
        float ph = 0.0f;
        const float decN = 0.055f + 0.11f * tone, decB = 0.06f - 0.015f * tone;
        for (size_t i = 0; i < b.size(); ++i)
        {
            const float t = (float) i / fs;
            const float n = rng.next();
            const float click = hpC.tick (n).hp * std::exp (-t / 0.0025f) * 1.4f;
            const float f = 185.0f + 190.0f * std::exp (-t / 0.011f);
            ph += f / fs; if (ph >= 1.0f) ph -= 1.0f;
            const float body = (std::sin (kTwoPi * ph) + 0.32f * std::sin (2.0f * kTwoPi * ph)) * std::exp (-t / decB) * std::min (1.0f, t / 0.0008f) * 1.25f;
            const float noise = (hpN.tick (n).hp * 0.7f + bpN.tick (n).bp * 1.3f) * std::exp (-t / decN) * (0.9f + 0.4f * tone);
            b[i] = click + body + noise;
        }
        // glue: drive into a soft clipper, then a short room so it sounds like a record, not a test tone
        for (auto& v : b) v = std::tanh (1.7f * v) / std::tanh (1.7f);
        const int taps[3] = { (int) (0.011f * fs), (int) (0.019f * fs), (int) (0.029f * fs) };
        for (size_t i = b.size(); i-- > 0;)
        {
            float add = 0.0f;
            for (int k = 0; k < 3; ++k) if ((int) i >= taps[k]) add += b[i - (size_t) taps[k]] * (0.12f - 0.03f * k);
            b[i] += add;
        }
        normalise (b, 0.97f);
    }

    void renderClap()
    {
        auto& b = s[1];
        Rng rng; rng.s = 0x7654321u;
        Svf bp, hp; bp.set (fs, 1250.0f, 1.3f); hp.set (fs, 700.0f, 0.7f);
        for (size_t i = 0; i < b.size(); ++i)
        {
            const float t = (float) i / fs;
            float env = 0.0f;
            for (int k = 0; k < 4; ++k) { const float tt = t - 0.0105f * (float) k; if (tt >= 0.0f) env = std::max (env, std::exp (-tt / 0.0045f)); }
            if (t > 0.032f) env = std::max (env, 0.7f * std::exp (-(t - 0.032f) / 0.16f));
            const float n = rng.next();
            b[i] = (bp.tick (n).bp * 2.2f + hp.tick (n).hp * 0.4f) * env;
        }
        for (auto& v : b) v = std::tanh (2.2f * v);
        normalise (b, 0.95f);
    }

    void renderKick()
    {
        auto& b = s[2];
        Rng rng; float ph = 0.0f;
        for (size_t i = 0; i < b.size(); ++i)
        {
            const float t = (float) i / fs;
            const float f = 48.0f + 210.0f * std::exp (-t / 0.028f);
            ph += f / fs; if (ph >= 1.0f) ph -= 1.0f;
            float y = std::sin (kTwoPi * ph) * std::exp (-t / 0.42f) * std::min (1.0f, t / 0.0006f);
            y += rng.next() * std::exp (-t / 0.0012f) * 0.5f;
            b[i] = std::tanh (2.4f * y);
        }
        normalise (b, 0.97f);
    }

    void renderHat (std::vector<float>& b, float dec)
    {
        Rng rng; rng.s = (juce::uint32) (dec * 100000.0f) + 99u;
        Svf hp, bp; hp.set (fs, 7500.0f, 0.7f); bp.set (fs, 10500.0f, 1.4f);
        for (size_t i = 0; i < b.size(); ++i)
        {
            const float t = (float) i / fs;
            const float n = rng.next();
            b[i] = (hp.tick (n).hp * 0.8f + bp.tick (n).bp * 1.0f) * std::exp (-t / dec) * std::min (1.0f, t / 0.0004f);
        }
        normalise (b, dec < 0.1f ? 0.75f : 0.6f);
    }
};

// Pitched playback of a kit sound with an optional gate (DECAY knob)
struct SampleVoice
{
    const std::vector<float>* buf = nullptr;
    double pos = 0.0, rate = 1.0;
    float vel = 1.0f, gate = 1e9f, t = 0.0f, fs = 44100.0f, pan = 0.5f;
    int sound = 0;
    bool active = false;

    void trigger (const std::vector<float>& b, int snd, float sampleRate, float semis, float velocity, float gateSeconds, float panPos)
    {
        buf = &b; sound = snd; fs = sampleRate; rate = std::pow (2.0, semis / 12.0) * 1.0; pos = 0.0;
        vel = velocity; gate = gateSeconds; t = 0.0f; pan = panPos; active = true;
    }

    inline float tick()
    {
        if (! active) return 0.0f;
        const auto n = buf->size();
        const auto i = (size_t) pos;
        if (i + 1 >= n) { active = false; return 0.0f; }
        const float fr = (float) (pos - (double) i);
        float y = (*buf)[i] + ((*buf)[i + 1] - (*buf)[i]) * fr;
        if (t > gate) { const float g = 1.0f - (t - gate) / 0.012f; if (g <= 0.0f) { active = false; return 0.0f; } y *= g; }
        pos += rate; t += 1.0f / fs;
        return y * vel;
    }
};

// Arpeggio pluck: one saw voice with a decaying low-pass
struct ArpVoice
{
    float fs = 44100, ph = 0, hz = 220, t = 10, vel = 1;
    Svf lp;
    void trigger (float sampleRate, float freq, float velocity) { fs = sampleRate; hz = freq; t = 0; vel = velocity; }
    inline float tick (float open)
    {
        if (t > 1.5f) return 0.0f;
        const float dt = hz / fs;
        ph += dt; if (ph >= 1.0f) ph -= 1.0f;
        const float saw = (2.0f * ph - 1.0f) - PadSynth::polyBlep (ph, dt);
        lp.set (fs, 300.0f + (1500.0f + 7000.0f * open) * std::exp (-t / 0.09f), 1.1f);
        const float y = lp.tick (saw).lp * std::exp (-t / 0.25f) * std::min (1.0f, t / 0.002f);
        t += 1.0f / fs;
        return y * vel;
    }
};
}
