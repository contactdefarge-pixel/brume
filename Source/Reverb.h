#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>
#include <cmath>

// Lush stereo algorithmic reverb: pre-delay -> input diffusion -> 8-line modulated feedback delay network.
// Every parameter glides smoothly (no rebuild), so turning Length or Character never cuts the tail.
namespace brume
{
class FdnReverb
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const int maxDelay = (int) (fs * 0.45) + 8;   // longest line at maximum size, plus modulation
        for (auto& l : lines) { l.buf.assign ((size_t) maxDelay, 0.0f); l.pos = 0; l.lp = 0.0f; }
        for (auto& ch : diffusers)
            for (size_t k = 0; k < ch.size(); ++k)
                ch[k].buf.assign ((size_t) (fs * 0.06) + 4, 0.0f), ch[k].pos = 0;
        for (auto& p : preDelay) { p.assign ((size_t) (fs * 0.2) + 4, 0.0f); }
        prePos = 0;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            lfoPhase[i] = (double) i / (double) lines.size();
            curDelay[i] = targetDelay (i, 1.0);
        }
        curPre = 0.01 * fs;
        curGain.fill (0.0f);
        setParameters (3.0f, 0.5f, 0.8f, true);
    }

    void reset()
    {
        for (auto& l : lines) { std::fill (l.buf.begin(), l.buf.end(), 0.0f); l.lp = 0.0f; }
        for (auto& ch : diffusers) for (auto& d : ch) std::fill (d.buf.begin(), d.buf.end(), 0.0f);
        for (auto& p : preDelay) std::fill (p.begin(), p.end(), 0.0f);
    }

    // seconds = T60 of the tail, character 0..1 = room size (small room -> big hall),
    // brightness 0..1 = high-frequency decay in the tail
    void setParameters (float seconds, float character, float brightness, bool snap = false)
    {
        t60 = std::max (0.1f, seconds);
        size = 0.45 + 1.0 * character;                       // delay-line scale
        preTarget = (0.003 + 0.055 * character) * fs;         // pre-delay
        diffusion = 0.62f + 0.12f * character;
        const double cutoff = 2500.0 * std::pow (8.0, brightness);   // 2.5 kHz .. 20 kHz
        dampCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * std::min (cutoff, fs * 0.45) / fs);
        if (snap)
        {
            curPre = preTarget;
            for (size_t i = 0; i < lines.size(); ++i) curDelay[i] = targetDelay (i, size);
        }
    }

    // stereo in -> stereo out (100 % wet), in place
    void process (float* left, float* right, int n)
    {
        // per-block: gains from the current (gliding) delay lengths
        std::array<float, 8> gTarget {};
        for (size_t i = 0; i < lines.size(); ++i)
            gTarget[i] = (float) std::pow (10.0, -3.0 * curDelay[i] / (t60 * fs));

        const double glide = 1.0 - std::exp (-1.0 / (0.35 * fs));   // delay lengths glide over ~0.35 s
        const float gGlide = 1.0f - (float) std::exp (-1.0 / (0.05 * fs));
        // loudness compensation: a longer tail builds up more energy
        const float compTarget = (float) (std::pow (2.0 / t60, compExp) * std::pow (10.0, -(1.45 - size) * 4.0 / 20.0));
        const double lfoInc = 1.0 / fs;

        for (int s = 0; s < n; ++s)
        {
            curPre += (preTarget - curPre) * glide;

            comp += (compTarget - comp) * gGlide;
            // loudness compensation on the input, so energy already in the tail is never boosted
            float in[2] = { left[s] * comp, right[s] * comp };
            float x[2];
            for (int c = 0; c < 2; ++c)
            {
                // pre-delay (fractional)
                auto& pd = preDelay[(size_t) c];
                pd[(size_t) prePos] = in[c];
                x[c] = readFrac (pd, prePos, curPre);
                // input diffusion: 4 allpasses
                for (size_t k = 0; k < 4; ++k)
                {
                    auto& ap = diffusers[(size_t) c][k];
                    const int len = std::max (1, (int) (apBase[k] * (c == 0 ? 1.0 : 1.071) * size * fs));
                    int rp = ap.pos - len; if (rp < 0) rp += (int) ap.buf.size();
                    const float delayed = ap.buf[(size_t) rp];
                    const float v = x[c] + diffusion * delayed;
                    ap.buf[(size_t) ap.pos] = v;
                    if (++ap.pos >= (int) ap.buf.size()) ap.pos = 0;
                    x[c] = delayed - diffusion * v;
                }
            }
            if (++prePos >= (int) preDelay[0].size()) prePos = 0;

            // read the 8 lines (modulated, interpolated), damp, scale
            std::array<float, 8> y {};
            for (size_t i = 0; i < lines.size(); ++i)
            {
                curDelay[i] += (targetDelay (i, size) - curDelay[i]) * glide;
                curGain[i] += (gTarget[i] - curGain[i]) * gGlide;
                lfoPhase[i] += lfoInc * lfoRate[i];
                if (lfoPhase[i] >= 1.0) lfoPhase[i] -= 1.0;
                const double mod = std::sin (juce::MathConstants<double>::twoPi * lfoPhase[i]) * 0.0009 * fs;  // +-0.9 ms
                auto& l = lines[i];
                float v = readFrac (l.buf, l.pos, curDelay[i] + mod);
                l.lp = v + (l.lp - v) * dampCoef;                 // one-pole low-pass in the loop
                y[i] = l.lp * curGain[i];
            }

            // Hadamard feedback matrix (energy preserving)
            std::array<float, 8> h = y;
            for (int len = 1; len < 8; len <<= 1)
                for (int i = 0; i < 8; i += len << 1)
                    for (int j = i; j < i + len; ++j)
                    {
                        const float a = h[(size_t) j], b = h[(size_t) (j + len)];
                        h[(size_t) j] = a + b; h[(size_t) (j + len)] = a - b;
                    }
            const float norm = 0.35355339f;   // 1/sqrt(8)

            for (size_t i = 0; i < lines.size(); ++i)
            {
                auto& l = lines[i];
                const float inject = (i & 1) ? x[1] : x[0];
                l.buf[(size_t) l.pos] = h[i] * norm + inject;
                if (++l.pos >= (int) l.buf.size()) l.pos = 0;
            }

            left[s]  = (y[0] - y[2] + y[4] - y[6] + y[1] * 0.5f - y[7] * 0.5f) * 0.5f;
            right[s] = (y[1] - y[3] + y[5] - y[7] + y[0] * 0.5f - y[6] * 0.5f) * 0.5f;
        }
    }

private:
    struct Line { std::vector<float> buf; int pos = 0; float lp = 0.0f; };
    struct Allpass { std::vector<float> buf; int pos = 0; };

    double targetDelay (size_t i, double sz) const { return baseMs[i] * 0.001 * sz * fs; }

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
    std::array<Line, 8> lines;
    std::array<std::array<Allpass, 4>, 2> diffusers;
    std::array<std::vector<float>, 2> preDelay;
    int prePos = 0;
    double curPre = 0.0, preTarget = 0.0;
    std::array<double, 8> curDelay {}, lfoPhase {};
    std::array<float, 8> curGain {};
    double size = 1.0;
    float t60 = 3.0f, diffusion = 0.7f, dampCoef = 0.3f, comp = 1.0f;
public:
    float compExp = 0.5f;
private:

    static constexpr double baseMs[8]  = { 31.3, 37.9, 43.2, 51.7, 59.1, 66.9, 77.3, 87.1 };
    static constexpr double apBase[4]  = { 0.0047, 0.0036, 0.0127, 0.0093 };
    static constexpr double lfoRate[8] = { 0.11, 0.13, 0.17, 0.19, 0.23, 0.29, 0.31, 0.37 };
};
}
