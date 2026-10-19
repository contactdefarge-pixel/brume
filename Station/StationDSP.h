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
// Snare: pitched body + band-passed noise. Pitch moves both, tone moves the noise colour and length.
struct SnareVoice
{
    bool active = false;
    float t = 0, ph = 0, f0 = 185, decB = 0.1f, decN = 0.18f, mixN = 0.6f, vel = 1, fs = 44100;
    Svf bp, hp;
    Rng rng;

    void trigger (float sampleRate, float semis, float tone, float velocity, juce::uint32 seed)
    {
        fs = sampleRate;
        const float pm = std::pow (2.0f, semis / 12.0f);
        f0 = 185.0f * pm;
        decB = 0.09f / std::sqrt (pm);
        decN = (0.10f + 0.20f * tone) / std::pow (pm, 0.35f);
        mixN = 0.50f + 0.35f * tone;
        bp.set (fs, std::min (16000.0f, (2300.0f + 1800.0f * tone) * pm), 0.8f);
        hp.set (fs, std::min (12000.0f, 900.0f * pm), 0.7f);
        t = 0; ph = 0; vel = velocity; active = true; rng.s ^= seed * 2654435761u;
    }

    inline float tick()
    {
        if (! active) return 0.0f;
        const float eB = std::exp (-t / decB), eN = std::exp (-t / decN);
        const float f = f0 * (1.0f + 0.9f * std::exp (-t / 0.006f));
        ph += f / fs; if (ph >= 1.0f) ph -= 1.0f;
        const float body = std::sin (kTwoPi * ph) + 0.35f * std::sin (kTwoPi * ph * 1.6f);
        const float n = rng.next();
        const float noise = bp.tick (n).bp * 1.8f + hp.tick (n).hp * 0.35f;
        t += 1.0f / fs;
        if (t > decN * 7.0f && t > decB * 7.0f) active = false;
        return (body * eB * (1.0f - 0.5f * mixN) * 0.8f + noise * eN * mixN) * vel;
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
// One-shots: 0 impact, 1 crash, 2 clap (big), 3 swell (reversed noise rising to a bar line)
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
    inline void tick (float gainTarget, float fadeCoef, float open, float timbre, float& outL, float& outR)
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
                const float pan = 0.5f + 0.4f * spread[s] * (v % 2 == 0 ? 1.0f : -1.0f);
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
// bass note + triad folded into a fixed range, so chords move smoothly
inline std::array<int, 4> chordNotes (int key, int degree)
{
    static const int majS[] = { 0, 2, 4, 5, 7, 9, 11 }, minS[] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* sc = keyIsMajor (key) ? majS : minS;
    const int root = keyRoot (key);
    auto deg = [&] (int d) { return root + sc[d % 7] + 12 * (d / 7); };
    std::array<int, 4> n {};
    n[0] = 36 + (deg (degree) % 12);
    for (int i = 0; i < 3; ++i)
    {
        int m = deg (degree + 2 * i);
        while (m < 55) m += 12;
        while (m >= 67) m -= 12;
        n[(size_t) i + 1] = m;
    }
    return n;
}
}
