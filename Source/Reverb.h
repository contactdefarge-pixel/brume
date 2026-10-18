#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>
#include <cmath>

// Clean stereo plate reverb (after J. Dattorro, "Effect Design Part 1", JAES 1997), smooth and non-metallic.
// Character crossfades between two plates of FIXED sizes (small room / large hall): no delay line ever moves,
// so turning Character or Length never bends the pitch or cuts the sound.
namespace brume
{
class PlateTank
{
public:
    void prepare (double sampleRate, double sizeScale, double preDelaySeconds)
    {
        fs = sampleRate;
        const double k = fs / 29761.0 * sizeScale;
        auto mk = [&] (Line& l, double samples29k, int extra = 0)
        {
            l.len = std::max (2, (int) std::lround (samples29k * k));
            l.buf.assign ((size_t) (l.len + extra + 4), 0.0f);
            l.pos = 0;
        };
        const int exc = (int) std::ceil (16.0 * k) + 2;
        mk (inAp[0], 142); mk (inAp[1], 107); mk (inAp[2], 379); mk (inAp[3], 277);
        mk (modApL, 672, exc); mk (dL1, 4453); mk (apL2, 1800); mk (dL2, 3720);
        mk (modApR, 908, exc); mk (dR1, 4217); mk (apR2, 2656); mk (dR2, 3163);
        excursion = 16.0 * k;
        pre.len = std::max (1, (int) (preDelaySeconds * fs));
        pre.buf.assign ((size_t) pre.len + 4, 0.0f); pre.pos = 0;
        tapScale = k;
        halfLoopSeconds = ((672 + 4453 + 1800 + 3720) + (908 + 4217 + 2656 + 3163)) * 0.5 * k / fs;
        bw = lpL = lpR = fbL = fbR = 0.0f;
        lfo = 0.0;
    }

    void setDecay (float g)    { decay = g; }
    void setDamping (float d)  { damping = d; }
    double getHalfLoopSeconds() const { return halfLoopSeconds; }

    // mono in -> stereo out
    inline void tick (float in, float& outL, float& outR)
    {
        // pre-delay + input bandwidth
        float x = pre.readAt (pre.len); pre.write (in);
        bw += 0.9995f * (x - bw); x = bw;

        // input diffusion
        x = allpass (inAp[0], x, 0.75f);
        x = allpass (inAp[1], x, 0.75f);
        x = allpass (inAp[2], x, 0.625f);
        x = allpass (inAp[3], x, 0.625f);

        // tank modulation (slow, a fraction of a millisecond)
        lfo += 1.0 / fs;
        if (lfo >= 1000.0) lfo -= 1000.0;
        const double m1 = excursion * (1.0 + std::sin (juce::MathConstants<double>::twoPi * 0.73 * lfo)) * 0.5;
        const double m2 = excursion * (1.0 + std::sin (juce::MathConstants<double>::twoPi * 0.61 * lfo + 1.7)) * 0.5;

        // left half of the tank
        float a = modAllpass (modApL, x + fbR * decay, 0.70f, m1);
        dL1.write (a);
        a = dL1.readAt (dL1.len);
        lpL += (1.0f - damping) * (a - lpL);
        a = lpL * decay;
        a = allpass (apL2, a, 0.5f);
        dL2.write (a);
        const float endL = dL2.readAt (dL2.len);

        // right half
        float b = modAllpass (modApR, x + fbL * decay, 0.70f, m2);
        dR1.write (b);
        b = dR1.readAt (dR1.len);
        lpR += (1.0f - damping) * (b - lpR);
        b = lpR * decay;
        b = allpass (apR2, b, 0.5f);
        dR2.write (b);
        const float endR = dR2.readAt (dR2.len);

        fbL = endL; fbR = endR;

        // output taps (Dattorro's table, scaled)
        auto t = [this] (Line& l, int s29) { return l.readAt (std::max (1, (int) std::lround (s29 * tapScale))); };
        outL = 0.6f * ( t (dR1, 266) + t (dR1, 2974) - t (apR2, 1913) + t (dR2, 1996) - t (dL1, 1990) - t (apL2, 187) - t (dL2, 1066));
        outR = 0.6f * ( t (dL1, 353) + t (dL1, 3627) - t (apL2, 1228) + t (dL2, 2673) - t (dR1, 2111) - t (apR2, 335) - t (dR2, 121));
    }

private:
    struct Line
    {
        std::vector<float> buf; int len = 1, pos = 0;
        inline void write (float v) { buf[(size_t) pos] = v; if (++pos >= (int) buf.size()) pos = 0; }
        // value written `d` samples ago (d >= 1)
        inline float readAt (int d) const { int i = pos - d; while (i < 0) i += (int) buf.size(); return buf[(size_t) i]; }
        inline float readFrac (double d) const
        {
            const int i0 = (int) d; const float f = (float) (d - i0);
            return readAt (std::max (1, i0)) + (readAt (std::max (1, i0 + 1)) - readAt (std::max (1, i0))) * f;
        }
    };

    static inline float allpass (Line& l, float x, float g)
    {
        const float d = l.readAt (l.len);
        const float v = x + g * d;
        l.write (v);
        return d - g * v;
    }
    static inline float modAllpass (Line& l, float x, float g, double mod)
    {
        const float d = l.readFrac (l.len + mod);
        const float v = x - g * d;      // "decay diffusion 1" uses the inverted sign
        l.write (v);
        return d + g * v;
    }

    double fs = 44100.0, excursion = 16.0, tapScale = 1.0, halfLoopSeconds = 0.36, lfo = 0.0;
    Line pre, modApL, dL1, apL2, dL2, modApR, dR1, apR2, dR2;
    std::array<Line, 4> inAp;
    float bw = 0, lpL = 0, lpR = 0, fbL = 0, fbR = 0, decay = 0.5f, damping = 0.3f;
};

class FdnReverb   // (name kept for the rest of the code) two plates crossfaded by Character
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        small.prepare (fs, 0.62, 0.004);
        large.prepare (fs, 1.30, 0.030);
        setParameters (3.0f, 0.5f, 0.7f, true);
    }

    void setParameters (float seconds, float character, float brightness, bool snap = false)
    {
        t60 = std::max (0.15f, seconds);
        charTarget = juce::jlimit (0.0f, 1.0f, character);
        // damping: 0 = bright, 1 = dark. Brightness 0..1 -> fairly warm .. open
        damp = juce::jlimit (0.05f, 0.85f, 0.95f - 0.50f * brightness);
        if (snap) { charCur = charTarget; comp = compTarget(); }
    }

    // stereo in -> stereo out (100 % wet), in place
    void process (float* left, float* right, int n)
    {
        const auto decayFor = [this] (const PlateTank& p)
        {
            return (float) juce::jlimit (0.0, 0.985, std::pow (10.0, -3.0 * p.getHalfLoopSeconds() / t60));
        };
        small.setDecay (decayFor (small)); small.setDamping (damp);
        large.setDecay (decayFor (large)); large.setDamping (damp);

        const float glide = 1.0f - std::exp (-1.0f / (0.08f * (float) fs));
        const float ct = compTarget();
        for (int s = 0; s < n; ++s)
        {
            charCur += (charTarget - charCur) * glide;
            comp += (ct - comp) * glide;
            const float in = (left[s] + right[s]) * 0.5f * comp;
            float sl, sr, ll, lr;
            small.tick (in, sl, sr);
            large.tick (in, ll, lr);
            // equal-power crossfade
            const float a = std::cos (charCur * 1.5707963f), b = std::sin (charCur * 1.5707963f);
            left[s]  = sl * a + ll * b;
            right[s] = sr * a + lr * b;
        }
    }

private:
    // a longer tail builds up more energy: compensate on the input so knob moves keep a steady level
    float compTarget() const { return (float) std::pow (2.0 / t60, 0.12); }

    double fs = 44100.0;
    PlateTank small, large;
    float t60 = 3.0f, charTarget = 0.5f, charCur = 0.5f, damp = 0.3f, comp = 1.0f;
};
}
