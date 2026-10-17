#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>
#include <cmath>

// Lush stereo algorithmic reverb:
//   pre-delay -> early reflections + input diffusion -> 16-line modulated feedback delay network.
// Every parameter glides smoothly (nothing is rebuilt), so turning Length or Character never cuts the sound.
namespace brume
{
class FdnReverb
{
public:
    static constexpr int N = 16;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const int maxDelay = (int) (fs * 0.30) + 8;   // longest line at maximum size, plus modulation
        for (int i = 0; i < N; ++i)
        {
            auto& l = lines[(size_t) i];
            l.buf.assign ((size_t) maxDelay, 0.0f); l.pos = 0; l.lp = 0.0f; l.hp = 0.0f;
            l.ap.assign ((size_t) std::max (8, (int) (lineApMs[(size_t) i] * 0.001 * fs)), 0.0f); l.apPos = 0;
        }
        for (auto& ch : diffusers)
            for (auto& d : ch) { d.buf.assign ((size_t) (fs * 0.05) + 4, 0.0f); d.pos = 0; }
        for (auto& p : preDelay) p.assign ((size_t) (fs * 0.25) + 4, 0.0f);
        prePos = 0;
        for (int i = 0; i < N; ++i)
        {
            lfoPhase[(size_t) i] = (double) i / (double) N;
            curDelay[(size_t) i] = targetDelay (i, 1.0);
        }
        curGain.fill (0.0f);
        setParameters (3.0f, 0.5f, 0.8f, true);
    }

    // seconds = T60 of the tail, character 0..1 = room size (small room -> big hall),
    // brightness 0..1 = how long the highs ring in the tail
    void setParameters (float seconds, float character, float brightness, bool snap = false)
    {
        t60 = std::max (0.15f, seconds);
        size = 0.5 + 0.9 * character;
        preTarget = (0.004 + 0.050 * character) * fs;
        erSpread = 0.35 + 0.65 * character;
        erLevel = 0.55f - 0.25f * character;               // small rooms: stronger early reflections
        diffusion = 0.68f + 0.08f * character;
        const double cutoff = 1500.0 * std::pow (6.0, brightness);   // 1.5 kHz .. 9 kHz: warm tail, highs die first
        dampCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * std::min (cutoff, fs * 0.45) / fs);
        if (snap)
        {
            curPre = preTarget;
            curSpread = erSpread;
            for (int i = 0; i < N; ++i) curDelay[(size_t) i] = targetDelay (i, size);
        }
    }

    // stereo in -> stereo out (100 % wet), in place
    void process (float* left, float* right, int n)
    {
        std::array<float, N> gTarget {};
        for (int i = 0; i < N; ++i)
            gTarget[(size_t) i] = (float) std::pow (10.0, -3.0 * curDelay[(size_t) i] / (t60 * fs));

        const double glide = 1.0 - std::exp (-1.0 / (0.35 * fs));   // sizes glide over ~0.35 s
        const float gGlide = 1.0f - (float) std::exp (-1.0 / (0.05 * fs));
        // loudness compensation applied on the input (energy already in the tail is never boosted)
        const float compTarget = (float) (std::pow (2.0 / t60, 0.33) * std::pow (10.0, (size - 0.95) / 0.9 * 6.0 / 20.0));
        const double lfoInc = 1.0 / fs;
        const float norm = 0.25f;   // 1/sqrt(16)
        const float hpCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * 60.0 / fs);   // keep the tail out of the sub

        for (int s = 0; s < n; ++s)
        {
            curPre += (preTarget - curPre) * glide;
            curSpread += (erSpread - curSpread) * glide;
            comp += (compTarget - comp) * gGlide;

            float x[2], er[2] = { 0.0f, 0.0f };
            const float in[2] = { left[s] * comp, right[s] * comp };
            for (int c = 0; c < 2; ++c)
            {
                auto& pd = preDelay[(size_t) c];
                pd[(size_t) prePos] = in[c];
                x[c] = readFrac (pd, prePos, curPre);

                // early reflections: 6 taps after the pre-delay, mirrored between channels
                for (int t = 0; t < 6; ++t)
                {
                    const double d = curPre + erMs[(size_t) t][(size_t) c] * 0.001 * fs * curSpread;
                    er[c] += readFrac (pd, prePos, d) * erGain[(size_t) t];
                }

                // input diffusion: 6 allpasses
                for (size_t k = 0; k < 6; ++k)
                {
                    auto& ap = diffusers[(size_t) c][k];
                    const int len = std::max (1, (int) (apBase[k] * (c == 0 ? 1.0 : 1.083) * size * fs));
                    int rp = ap.pos - len; if (rp < 0) rp += (int) ap.buf.size();
                    const float delayed = ap.buf[(size_t) rp];
                    const float v = x[c] + diffusion * delayed;
                    ap.buf[(size_t) ap.pos] = v;
                    if (++ap.pos >= (int) ap.buf.size()) ap.pos = 0;
                    x[c] = delayed - diffusion * v;
                }
            }
            if (++prePos >= (int) preDelay[0].size()) prePos = 0;

            // read the lines (modulated + interpolated), damp, scale
            std::array<float, N> y {};
            for (int i = 0; i < N; ++i)
            {
                const auto ii = (size_t) i;
                curDelay[ii] += (targetDelay (i, size) - curDelay[ii]) * glide;
                curGain[ii] += (gTarget[ii] - curGain[ii]) * gGlide;
                lfoPhase[ii] += lfoInc * (0.07 + 0.031 * i);
                if (lfoPhase[ii] >= 1.0) lfoPhase[ii] -= 1.0;
                const double ph = juce::MathConstants<double>::twoPi * lfoPhase[ii];
                const double mod = (std::sin (ph) * 0.7 + std::sin (ph * 2.618 + i) * 0.3) * 0.0012 * fs;   // +-1.2 ms, non-periodic
                auto& l = lines[ii];
                const float v = readFrac (l.buf, l.pos, curDelay[ii] + mod);
                l.lp = v + (l.lp - v) * dampCoef;              // highs decay faster
                l.hp = l.lp + (l.hp - l.lp) * hpCoef;          // very lows don't build up
                float z = l.lp - l.hp;
                {   // in-loop allpass (g = 0.6)
                    const float d = l.ap[(size_t) l.apPos];
                    const float v2 = z + 0.6f * d;
                    l.ap[(size_t) l.apPos] = v2;
                    if (++l.apPos >= (int) l.ap.size()) l.apPos = 0;
                    z = d - 0.6f * v2;
                }
                y[ii] = z * curGain[ii];
            }

            // 16x16 Hadamard feedback (energy preserving)
            std::array<float, N> h = y;
            for (int len = 1; len < N; len <<= 1)
                for (int i = 0; i < N; i += len << 1)
                    for (int j = i; j < i + len; ++j)
                    {
                        const float a = h[(size_t) j], b = h[(size_t) (j + len)];
                        h[(size_t) j] = a + b; h[(size_t) (j + len)] = a - b;
                    }

            for (int i = 0; i < N; ++i)
            {
                auto& l = lines[(size_t) i];
                l.buf[(size_t) l.pos] = h[(size_t) i] * norm + ((i & 1) ? x[1] : x[0]) * 0.7f;
                if (++l.pos >= (int) l.buf.size()) l.pos = 0;
            }

            // decorrelated stereo taps from the lines
            float outL = 0.0f, outR = 0.0f;
            for (int i = 0; i < N; ++i)
            {
                outL += y[(size_t) i] * tapL[(size_t) i];
                outR += y[(size_t) i] * tapR[(size_t) i];
            }
            left[s]  = outL * 0.35f + er[0] * erLevel * 0.35f;
            right[s] = outR * 0.35f + er[1] * erLevel * 0.35f;
        }
    }

private:
    struct Line { std::vector<float> buf; int pos = 0; float lp = 0.0f, hp = 0.0f; std::vector<float> ap; int apPos = 0; };
    struct Allpass { std::vector<float> buf; int pos = 0; };

    double targetDelay (int i, double sz) const { return baseMs[(size_t) i] * 0.001 * sz * fs; }

    static float readFrac (const std::vector<float>& buf, int writePos, double delay)
    {
        const int size = (int) buf.size();
        delay = juce::jlimit (1.0, (double) size - 3.0, delay);
        double rp = (double) writePos - delay;
        while (rp < 0) rp += size;
        const int i0 = (int) rp;
        const float frac = (float) (rp - i0);
        const int i1 = (i0 + 1) % size;
        return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * frac;
    }

    double fs = 44100.0;
    std::array<Line, N> lines;
    std::array<std::array<Allpass, 6>, 2> diffusers;
    std::array<std::vector<float>, 2> preDelay;
    int prePos = 0;
    double curPre = 0.0, preTarget = 0.0, curSpread = 1.0, erSpread = 1.0;
    std::array<double, N> curDelay {}, lfoPhase {};
    std::array<float, N> curGain {};
    double size = 1.0;
    float t60 = 3.0f, diffusion = 0.7f, dampCoef = 0.3f, comp = 1.0f, erLevel = 0.4f;

    // mutually prime-ish line lengths (ms at size 1), 23..143 ms
    static constexpr double baseMs[N] = { 23.1, 26.9, 31.7, 35.3, 41.9, 46.1, 52.3, 57.7,
                                          64.1, 71.3, 78.7, 86.9, 96.1, 107.3, 119.9, 133.1 };
    static constexpr double apBase[6] = { 0.0047, 0.0036, 0.0127, 0.0093, 0.0071, 0.0161 };
    // short allpass inside every feedback line: multiplies echo density, kills metallic ringing
    static constexpr double lineApMs[N] = { 2.3, 3.1, 3.7, 4.3, 2.9, 5.3, 4.7, 6.1, 3.3, 5.9, 2.7, 6.7, 4.1, 7.3, 3.9, 5.1 };
    static constexpr double erMs[6][2] = { { 7.1, 9.3 }, { 13.7, 11.9 }, { 19.3, 23.1 }, { 29.9, 27.1 }, { 37.3, 41.7 }, { 53.9, 49.1 } };
    static constexpr float erGain[6] = { 0.62f, -0.51f, 0.44f, -0.37f, 0.30f, -0.24f };
    static constexpr float tapL[N] = { 1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, 1, -1, 1, 1, -1 };
    static constexpr float tapR[N] = { 1, 1, -1, 1, 1, -1, -1, 1, -1, 1, 1, -1, -1, -1, 1, 1 };
};
}
