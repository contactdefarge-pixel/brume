#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Levels (calibrated with Tests/LevelTest.cpp)
    constexpr float kReverbGain  = 1.5f;    // algorithmic reverb output
    constexpr float kWetGain     = 1.4f;
    constexpr float kMasterGain  = 0.9f;
    constexpr float kNoiseMax    = 0.80f;   // louder noise: about +9 dB at mid knob, +5.6 dB at max
    constexpr float kNoiseDirect = 0.85f;
    constexpr float kNoiseWash   = 0.6f;

    float softClip (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.9f) return x;
        const float y = 0.9f + 0.1f * std::tanh ((a - 0.9f) / 0.1f);
        return x < 0.0f ? -y : y;
    }

    juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
}

//==============================================================================
BrumeProcessor::BrumeProcessor (bool embedded)
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BRUME", createLayout())
{
    using namespace brume;
    pReverb    = apvts.getRawParameterValue (id::reverb);
    pCharacter = apvts.getRawParameterValue (id::character);
    pLength    = apvts.getRawParameterValue (id::length);
    pAir       = apvts.getRawParameterValue (id::air);
    pNoise     = apvts.getRawParameterValue (id::noise);
    pTremolo   = apvts.getRawParameterValue (id::tremolo);
    pTempo     = apvts.getRawParameterValue (id::tempo);
    pFilter    = apvts.getRawParameterValue (id::filter);
    pWet       = apvts.getRawParameterValue (id::wet);
    pOn        = apvts.getRawParameterValue (id::on);

    for (auto& m : midiMap) m.store (-1);
    if (embedded) return;
    startTimerHz (20);
    if (juce::JUCEApplicationBase::isStandaloneApp())
        f1Reader.start();
}

BrumeProcessor::~BrumeProcessor() { stopTimer(); stopRecording(); }

bool BrumeProcessor::startRecording()
{
    stopRecording();
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Brume");
    dir.createDirectory();
    auto file = dir.getNonexistentChildFile ("Brume " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"), ".wav");
    auto stream = file.createOutputStream();
    if (stream == nullptr) return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (stream.release());
    auto* writer = wav.createWriterFor (os.get(), fs, 2, 24, {}, 0);
    if (writer == nullptr) return false;
    os.release();   // the writer owns the stream now

    recThread.startThread();
    recWriter.reset (new juce::AudioFormatWriter::ThreadedWriter (writer, recThread, 32768));
    recSamples = 0;
    lastRecording = file;
    const juce::ScopedLock sl (recLock);
    activeWriter = recWriter.get();
    return true;
}

void BrumeProcessor::stopRecording()
{
    {
        const juce::ScopedLock sl (recLock);
        activeWriter = nullptr;
    }
    recWriter.reset();   // flushes and closes the file
}

juce::AudioProcessorValueTreeState::ParameterLayout BrumeProcessor::createLayout()
{
    using namespace brume;
    using P = juce::AudioParameterFloat;
    using A = juce::AudioParameterFloatAttributes;
    juce::NormalisableRange<float> r (0.0f, 1.0f);
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;

    ps.push_back (std::make_unique<P> (juce::ParameterID { id::reverb, 1 }, "Reverb", r, 0.65f, A().withStringFromValueFunction (pct)));
    ps.push_back (std::make_unique<P> (juce::ParameterID { id::character, 1 }, "Character", r, 0.5f,
        A().withStringFromValueFunction ([] (float v, int) {
            return juce::String (v < 0.34f ? "room " : v > 0.66f ? "hall " : "studio ") + juce::String (juce::roundToInt (v * 100.0f)); })));
    ps.push_back (std::make_unique<P> (juce::ParameterID { id::length, 1 }, "Length", r, 0.62f,
        A().withStringFromValueFunction ([] (float v, int) {
            const float s = lengthSeconds (v); return juce::String (s, s < 10.0f ? 1 : 0) + " s"; })));
    ps.push_back (std::make_unique<P> (juce::ParameterID { id::air, 1 }, "Air", r, 0.3f,
        A().withStringFromValueFunction ([] (float v, int) { return "+" + juce::String (v * 10.0f, 1) + " dB"; })));
    ps.push_back (std::make_unique<P> (juce::ParameterID { id::noise, 1 }, "Noise", r, 0.5f, A().withStringFromValueFunction (pct)));
    ps.push_back (std::make_unique<P> (juce::ParameterID { id::tremolo, 1 }, "Tremolo", r, 0.8f, A().withStringFromValueFunction (pct)));
    ps.push_back (std::make_unique<P> (juce::ParameterID { id::tempo, 1 }, "Tempo", r, 0.568f,
        A().withStringFromValueFunction ([] (float v, int) {
            const float hz = tremoloRateHz (v);
            return juce::String (hz, hz < 10.0f ? 2 : 1) + " Hz | sync " + division (v).label; })));
    ps.push_back (std::make_unique<P> (juce::ParameterID { id::filter, 1 }, "Filter", r, 0.3f,
        A().withStringFromValueFunction ([] (float v, int) {
            const float hz = lowCutHz (v);
            return hz < 1000.0f ? "low cut " + juce::String (juce::roundToInt (hz)) + " Hz"
                                : "low cut " + juce::String (hz / 1000.0f, 1) + " kHz"; })));
    ps.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::wet, 1 }, "Mix/Wet", false,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool b, int) { return b ? "Wet" : "Mix"; })));
    ps.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::on, 1 }, "On/Off", false,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool b, int) { return b ? "On" : "Off"; })));

    return { ps.begin(), ps.end() };
}

bool BrumeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

//==============================================================================
void BrumeProcessor::noiseFromSample (float& l, float& r)
{
    l = r = 0.0f;
    const int len = (int) nsL.size();
    for (auto& g : grains)
    {
        if (g.age >= grainLen)   // grain finished: start a new one somewhere else in the recording
        {
            g.age = 0;
            g.pos = (double) noiseRandom.nextInt (juce::jmax (1, len - grainLen - 2));
        }
        const float w = grainWin[(size_t) g.age];
        const auto i = (size_t) g.pos;
        l += nsL[i] * w; r += nsR[i] * w;
        g.pos += 1.0; ++g.age;
    }
}

void BrumeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxBlock = std::max (1, samplesPerBlock);
    if (! rawNoiseL.empty())
    {
        // resample the recording to the session rate, set up the grains (sqrt-Hann: constant power for noise)
        const double ratio = rawNoiseRate / fs;
        const int outLen = (int) ((double) rawNoiseL.size() / ratio) - 2;
        nsL.resize ((size_t) juce::jmax (8, outLen)); nsR.resize (nsL.size());
        for (size_t k = 0; k < nsL.size(); ++k)
        {
            const double p = (double) k * ratio; const auto j = (size_t) p; const float fr = (float) (p - (double) j);
            nsL[k] = rawNoiseL[j] + (rawNoiseL[j + 1] - rawNoiseL[j]) * fr;
            nsR[k] = rawNoiseR[j] + (rawNoiseR[j + 1] - rawNoiseR[j]) * fr;
        }
        grainLen = juce::jmin ((int) (0.15 * fs), (int) nsL.size() / 3);
        grainWin.resize ((size_t) grainLen);
        for (int k = 0; k < grainLen; ++k)
            grainWin[(size_t) k] = std::sqrt (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) k / (float) grainLen));
        grains[0] = { 0.0, 0 };
        grains[1] = { (double) (nsL.size() / 2), grainLen / 2 };
    }
    f1Collector.reset (fs);
    f1Midi.ensureSize (4096);
    f1Ready = true;

    juce::dsp::ProcessSpec spec { fs, (juce::uint32) maxBlock, 2 };
    {
        const float l = brume::lengthSeconds (pLength->load()), c = pCharacter->load(), b = 0.55f + 0.45f * pAir->load();
        fdn.prepare (fs);      fdn.setParameters (l, c, b, true);
        fdnNoise.prepare (fs); fdnNoise.setParameters (l, c, b, true);
    }

    juce::dsp::ProcessSpec mono { fs, (juce::uint32) maxBlock, 1 };
    auto setup = [&] (Filter* f, juce::dsp::IIR::Coefficients<float>::Ptr coefs)
    {
        for (int c = 0; c < 2; ++c) { f[c].coefficients = coefs; f[c].prepare (mono); f[c].reset(); }
    };
    setup (preHP,    juce::dsp::IIR::Coefficients<float>::makeHighPass (fs, 160.0f, 0.707f));
    setup (airShelf, juce::dsp::IIR::Coefficients<float>::makeHighShelf (fs, 2400.0f, 0.707f, 1.0f));
    setup (lowCut,   juce::dsp::IIR::Coefficients<float>::makeHighPass (fs, 160.0f, 1.1f));
    // Revolo voicing (measured on recordings of the pedal): flat 160 Hz - 8 kHz, rolled off below ~170 Hz and above ~10 kHz
    setup (noiseHP,  juce::dsp::IIR::Coefficients<float>::makeHighPass (fs, revoloNoise ? 130.0f : 35.0f, 0.707f));
    setup (noiseLP,  juce::dsp::IIR::Coefficients<float>::makeLowPass (fs, std::min (revoloNoise ? 13500.0 : 17000.0, fs * 0.45), 0.707f));

    sendBuf.setSize (2, maxBlock);
    noiseBuf.setSize (2, maxBlock);
    washBuf.setSize (2, maxBlock);

    lowCutSmoothed = brume::lowCutHz (pFilter->load());
    lastPreHP = lastAir = lastLowCut = -1.0f;
    updateFilters (true);

    dryGain  = pWet->load() > 0.5f ? 0.0f : 1.0f;
    sendAmt  = pReverb->load() * pReverb->load() * 1.2f;
    noiseAmt = std::pow (pNoise->load(), 0.8f) * kNoiseMax;
    tremDepth = pTremolo->load();
    sendGate = noiseGate = pOn->load() > 0.5f ? 1.0f : 0.0f;

    prepared = true;
}

void BrumeProcessor::timerCallback() {}

void BrumeProcessor::updateFilters (bool force)
{
    using AC = juce::dsp::IIR::ArrayCoefficients<float>;
    const float nyq = (float) fs * 0.45f;

    const float pre = 220.0f - pCharacter->load() * 120.0f;
    if (force || std::abs (pre - lastPreHP) > 0.5f)
    {
        *preHP[0].coefficients = AC::makeHighPass (fs, pre, 0.707f);
        lastPreHP = pre;
    }

    const float air = pAir->load();
    if (force || std::abs (air - lastAir) > 0.001f)
    {
        *airShelf[0].coefficients = AC::makeHighShelf (fs, 2400.0f, 0.707f, juce::Decibels::decibelsToGain (air * 10.0f));
        lastAir = air;
    }

    if (force || std::abs (lowCutSmoothed - lastLowCut) > 0.05f)
    {
        *lowCut[0].coefficients = AC::makeHighPass (fs, juce::jlimit (20.0f, nyq, lowCutSmoothed), 1.1f);
        lastLowCut = lowCutSmoothed;
    }
}

//==============================================================================
void BrumeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const juce::int64 blockStart = sampleCounter;
    for (const auto meta : midi)
        handleMidi (meta.getMessage(), blockStart + meta.samplePosition);
    midi.clear();
    if (f1Ready.load())
    {
        f1Midi.clear();
        f1Collector.removeNextBlockOfMessages (f1Midi, std::max (1, buffer.getNumSamples()));
        for (const auto meta : f1Midi)
            handleMidi (meta.getMessage(), blockStart + meta.samplePosition);
    }
    sampleCounter += buffer.getNumSamples();

    // tempo source: MIDI clock first (Traktor), then the host's tempo (Ableton), else free-running
    int src = 0;
    double bpm = 0.0, beatAtStart = beatPos;
    if (clockTickSamples > 0.0 && lastClockSample >= 0 && blockStart - lastClockSample < (juce::int64) (0.5 * fs))
    {
        src = 1;
        bpm = fs * 60.0 / (clockTickSamples * 24.0);
        beatAtStart = (clockTicks + (double) (blockStart - lastClockSample) / clockTickSamples) / 24.0;
    }
    else if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
            if (auto hostBpm = pos->getBpm(); hostBpm.hasValue() && *hostBpm > 1.0)
            {
                src = 2;
                bpm = *hostBpm;
                if (pos->getIsPlaying())
                    if (auto ppq = pos->getPpqPosition()) beatAtStart = *ppq;
            }
    }
    syncSource.store (src);
    syncBpm.store ((float) bpm);

    // process in slices no longer than the size we prepared for (some hosts send bigger blocks)
    const int total = buffer.getNumSamples();
    const double beatInc = bpm / 60.0 / fs;
    for (int start = 0; start < total; start += maxBlock)
    {
        const int len = std::min (maxBlock, total - start);
        juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, len);
        processChunk (slice, src, bpm, src == 0 ? beatPos : beatAtStart + start * beatInc);
    }

    // recorder (lock-free FIFO to a background thread)
    const juce::ScopedTryLock sl (recLock);
    if (sl.isLocked())
        if (auto* w = activeWriter.load())
        {
            w->write (buffer.getArrayOfReadPointers(), total);
            recSamples += total;
        }
}

void BrumeProcessor::processChunk (juce::AudioBuffer<float>& buffer, int src, double bpm, double beatAtStart)
{
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    if (n == 0 || numOut == 0) return;

    // targets
    const float rv = pReverb->load();
    const float tSend  = rv * rv * 1.2f;
    const float tNoise = std::pow (pNoise->load(), 0.8f) * kNoiseMax;
    const float tTrem  = pTremolo->load();
    const float tDry   = pWet->load() > 0.5f ? 0.0f : 1.0f;
    const float tGate  = pOn->load() > 0.5f ? 1.0f : 0.0f;
    const double rate  = brume::tremoloRateHz (pTempo->load());

    // low cut: glide in the log domain to avoid zipper noise
    {
        const float target = brume::lowCutHz (pFilter->load());
        const float k = 1.0f - std::exp (-(float) n / (0.03f * (float) fs));
        lowCutSmoothed = std::exp (std::log (lowCutSmoothed) + (std::log (target) - std::log (lowCutSmoothed)) * k);
    }
    updateFilters (false);

    const auto coef = [this] (double tc) { return (float) std::exp (-1.0 / (tc * fs)); };
    const float cParam   = coef (0.02);
    const float cSendOn  = coef (0.006), cSendOff = coef (0.012);
    const float cNoiseOn = coef (0.02);
    // Off: the noise fades at the same rate as the reverb tail (T60 = Length)
    const float cNoiseOff = coef (brume::lengthSeconds (pLength->load()) / 6.9);

    const float* inL = buffer.getReadPointer (0);
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;

    auto* sL = sendBuf.getWritePointer (0);  auto* sR = sendBuf.getWritePointer (1);
    auto* nL = noiseBuf.getWritePointer (0); auto* nR = noiseBuf.getWritePointer (1);

    // 1) reverb send and noise source
    for (int i = 0; i < n; ++i)
    {
        sendAmt  = tSend  + (sendAmt  - tSend)  * cParam;
        // Hydra: turning NOISE down fades like ON/OFF does, at the pace of the reverb tail
        noiseAmt = tNoise + (noiseAmt - tNoise) * (slowNoiseDown && tNoise < noiseAmt ? cNoiseOff : cParam);
        sendGate  = tGate + (sendGate  - tGate) * (tGate > sendGate ? cSendOn : cSendOff);
        noiseGate = tGate + (noiseGate - tGate) * (tGate > noiseGate ? cNoiseOn : cNoiseOff);

        const float g = sendAmt * sendGate;
        sL[i] = preHP[0].processSample (inL[i] * g);
        sR[i] = preHP[1].processSample (inR[i] * g);

        if (! nsL.empty())   // the recorded noise is already voiced: no generator filters
        {
            float a, b; noiseFromSample (a, b);
            const float ng = noiseAmt * noiseGate * noiseTrim * 2.2f;
            nL[i] = a * ng; nR[i] = b * ng;
            continue;
        }
        // white noise, partly shared between L and R (correlation ~0.5)
        const float common = noiseRandom.nextFloat() * 2.0f - 1.0f;
        // Brume: L/R correlation ~0.5; Revolo: nearly independent channels (~0.15), a wide wash
        const float wc = revoloNoise ? 0.38f : 0.71f, wi = revoloNoise ? 0.92f : 0.71f;
        const float a = (common * wc + (noiseRandom.nextFloat() * 2.0f - 1.0f) * wi) * 0.5f;
        const float b = (common * wc + (noiseRandom.nextFloat() * 2.0f - 1.0f) * wi) * 0.5f;
        const float ng = noiseAmt * noiseGate * noiseTrim;
        nL[i] = noiseLP[0].processSample (noiseHP[0].processSample (a)) * ng;
        nR[i] = noiseLP[1].processSample (noiseHP[1].processSample (b)) * ng;
    }

    for (int c = 0; c < 2; ++c)
        washBuf.copyFrom (c, 0, noiseBuf, c, 0, n);

    // 2) reverbs (algorithmic: parameters glide, nothing is rebuilt, so no cut when turning knobs)
    {
        const float l = brume::lengthSeconds (pLength->load()), c = pCharacter->load(), b = 0.55f + 0.45f * pAir->load();
        fdn.setParameters (l, c, b);
        fdnNoise.setParameters (l, c, b);
        fdn.process (sL, sR, n);
        fdnNoise.process (washBuf.getWritePointer (0), washBuf.getWritePointer (1), n);
    }

    // 3) noise (direct + wash) with tremolo, summed into the wet bus, then Air and low cut
    const auto* wL = washBuf.getReadPointer (0); const auto* wR = washBuf.getReadPointer (1);
    const double inc = rate / fs;
    const bool lockTrem = src > 0 && ! freeTremolo;
    if (tapReset.exchange (false) && ! lockTrem) tremPhase = 0.0;
    // division with hysteresis, so a knob resting on a boundary doesn't flip between two divisions
    {
        const float pos = pTempo->load() * 9.0f;
        if (tremDivIdx < 0 || std::abs (pos - (float) tremDivIdx) > 0.62f)
            tremDivIdx = std::max (0, std::min (9, (int) std::lround (pos)));
    }
    const double cyclesPerBeat = brume::division ((float) tremDivIdx / 9.0f).cyclesPerBeat;
    const double beatInc = bpm / 60.0 / fs;
    const double lockCoef = 1.0 / (0.12 * fs);   // phase pulled onto the beat grid in ~0.1 s, never jumps
    for (int i = 0; i < n; ++i)
    {
        tremDepth = tTrem + (tremDepth - tTrem) * cParam;
        if (lockTrem)
        {
            // locked to the beat grid, but phase-continuous: changing division (or tempo) glides instead of cutting
            const double c = (beatAtStart + i * beatInc) * cyclesPerBeat;
            double err = (c - std::floor (c)) - tremPhase;
            err -= std::round (err);
            tremPhase += beatInc * cyclesPerBeat + err * std::min (1.0, lockCoef * (std::abs (err) > 0.25 ? 4.0 : 1.0));
            tremPhase -= std::floor (tremPhase);
        }
        else
        {
            tremPhase += inc;
            if (tremPhase >= 1.0) tremPhase -= 1.0;
        }
        const float trem = 1.0f - tremDepth * 0.5f + tremDepth * 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * tremPhase);

        float l = sL[i] * kReverbGain + (nL[i] * kNoiseDirect + wL[i] * kNoiseWash) * trem;
        float r = sR[i] * kReverbGain + (nR[i] * kNoiseDirect + wR[i] * kNoiseWash) * trem;
        l = lowCut[0].processSample (airShelf[0].processSample (l));
        r = lowCut[1].processSample (airShelf[1].processSample (r));
        sL[i] = l * kWetGain;
        sR[i] = r * kWetGain;
    }
    tremPhaseUi.store ((float) tremPhase);
    beatPos = beatAtStart + n * beatInc;

    // 4) output = dry (unless WET) + wet bus
    float* outL = buffer.getWritePointer (0);
    float* outR = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    float d = dryGain;
    for (int i = 0; i < n; ++i)
    {
        d = tDry + (d - tDry) * cParam;
        const float dl = inL[i], dr = inR[i];   // read before writing (in-place buffer)
        if (outR != nullptr)
        {
            outL[i] = softClip ((dl * d + sL[i]) * kMasterGain);
            outR[i] = softClip ((dr * d + sR[i]) * kMasterGain);
        }
        else
        {
            outL[i] = softClip (((dl + dr) * 0.5f * d + (sL[i] + sR[i]) * 0.5f) * kMasterGain);
        }
    }
    dryGain = d;

    for (int c = 2; c < numOut; ++c)
        buffer.clear (c, 0, n);
}

//==============================================================================
void BrumeProcessor::tap()
{
    if (syncSource.load() != 0 && ! freeTremolo) return;   // tempo comes from the clock
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (int i = taps.size(); --i >= 0;)
        if (now - taps[i] > 2200.0) taps.remove (i);
    taps.add (now);
    if (taps.size() < 2) return;

    double sum = 0.0;
    for (int i = 1; i < taps.size(); ++i) sum += taps[i] - taps[i - 1];
    const double avg = sum / (taps.size() - 1);
    const float hz = juce::jlimit (0.2f, 40.0f, (float) (1000.0 / avg));

    if (auto* p = apvts.getParameter (brume::id::tempo))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, brume::tempoFromRate (hz)));
        p->endChangeGesture();
    }
    tapReset.store (true);   // restart the tremolo cycle on the tap (picked up by the audio thread)
}

//==============================================================================
// MIDI learn

const char* BrumeProcessor::midiTargetName (int t)
{
    static const char* names[] = { "REVERB", "CHARACTER", "LENGTH", "AIR", "NOISE", "TREMOLO", "TEMPO", "FILTER", "MIX/WET", "ON/OFF", "TAP" };
    return juce::isPositiveAndBelow (t, numMidiTargets) ? names[t] : "";
}

juce::String BrumeProcessor::describeMapping (int code)
{
    if (code < 0) return {};
    const int type = (code >> 16) & 0xff, ch = (code >> 8) & 0xff, num = code & 0xff;
    if (ch == F1Reader::channel) return F1Reader::controlName (type, num);
    return juce::String (type == 1 ? "CC " : "Note ") + juce::String (num) + " (ch " + juce::String (ch) + ")";
}

void BrumeProcessor::setParamFromMidi (const char* paramId, float normalised)
{
    if (auto* p = apvts.getParameter (paramId))
        if (std::abs (p->getValue() - normalised) > 1.0e-4f)
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
}

void BrumeProcessor::handleMidi (const juce::MidiMessage& m, juce::int64 samplePos)
{
    // MIDI clock (24 ticks per beat), e.g. sent by Traktor
    if (m.isMidiClock())
    {
        if (lastClockSample >= 0)
        {
            const double d = (double) (samplePos - lastClockSample);
            if (d > 0.0 && d < fs * 0.25)
                clockTickSamples = clockTickSamples <= 0.0 ? d : clockTickSamples * 0.92 + d * 0.08;
        }
        lastClockSample = samplePos;
        clockTicks += 1.0;
        return;
    }
    if (m.isMidiStart())            { clockTicks = -1.0; return; }   // the next tick is beat 1
    if (m.isSongPositionPointer())  { clockTicks = m.getSongPositionPointerMidiBeat() * 6.0 - 1.0; return; }
    if (m.isMidiContinue() || m.isMidiStop()) return;

    int type = 0, num = 0, val = 0;
    if (m.isController())    { type = 1; num = m.getControllerNumber(); val = m.getControllerValue(); }
    else if (m.isNoteOn())   { type = 2; num = m.getNoteNumber(); val = 127; }
    else if (m.isNoteOff())  { type = 2; num = m.getNoteNumber(); val = 0; }
    else return;

    const int code = (type << 16) | (m.getChannel() << 8) | num;
    lastMidiCode.store (code); lastMidiValue.store (val); ++midiActivity;

    const int lt = learnTarget.load();
    if (lt >= 0)
    {
        if (val == 0 && type == 2) return;          // learn on the press, not the release
        for (auto& map : midiMap)                    // one control drives one target
            if (map.load() == code) map.store (-1);
        midiMap[(size_t) lt].store (code);
        midiLast[(size_t) lt] = val;
        learnTarget.store (-1);
        learnedTarget.store (lt);
        return;
    }

    static const char* knobIds[] = { brume::id::reverb, brume::id::character, brume::id::length, brume::id::air,
                                     brume::id::noise, brume::id::tremolo, brume::id::tempo, brume::id::filter };
    for (int t = 0; t < numMidiTargets; ++t)
    {
        if (midiMap[(size_t) t].load() != code) continue;
        const bool rising = val >= 64 && midiLast[(size_t) t] < 64;
        midiLast[(size_t) t] = val;

        if (t < 8)
        {
            if (type == 1) setParamFromMidi (knobIds[t], (float) val / 127.0f);
        }
        else if (t == 8 || t == 9)
        {
            // buttons send 127 when pressed: each press toggles
            if (rising)
                if (auto* p = apvts.getParameter (t == 8 ? brume::id::wet : brume::id::on))
                    p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        }
        else if (rising)
        {
            tapAtSample (samplePos);
        }
    }
}

void BrumeProcessor::tapAtSample (juce::int64 samplePos)
{
    if (syncSource.load() != 0 && ! freeTremolo) return;
    // same rule as the TAP footswitch, timed with the audio clock for accuracy
    const juce::int64 maxGap = (juce::int64) (2.2 * fs);
    if (numMidiTaps > 0 && samplePos - midiTaps[(size_t) numMidiTaps - 1] > maxGap)
        numMidiTaps = 0;
    if (numMidiTaps == (int) midiTaps.size())
    {
        std::move (midiTaps.begin() + 1, midiTaps.end(), midiTaps.begin());
        --numMidiTaps;
    }
    midiTaps[(size_t) numMidiTaps++] = samplePos;
    tapReset.store (true);
    if (numMidiTaps < 2) return;

    const double avgSeconds = (double) (midiTaps[(size_t) numMidiTaps - 1] - midiTaps[0]) / (numMidiTaps - 1) / fs;
    const float hz = juce::jlimit (0.2f, 40.0f, (float) (1.0 / avgSeconds));
    setParamFromMidi (brume::id::tempo, juce::jlimit (0.0f, 1.0f, brume::tempoFromRate (hz)));
}

//==============================================================================
void BrumeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::ValueTree map ("MIDIMAP");
    for (int t = 0; t < numMidiTargets; ++t)
        map.setProperty ("t" + juce::String (t), midiMap[(size_t) t].load(), nullptr);
    state.removeChild (state.getChildWithName ("MIDIMAP"), nullptr);
    state.appendChild (map, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BrumeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            const auto map = tree.getChildWithName ("MIDIMAP");
            for (int t = 0; t < numMidiTargets; ++t)
                midiMap[(size_t) t].store (map.isValid() ? (int) map.getProperty ("t" + juce::String (t), -1) : -1);
            tree.removeChild (map, nullptr);
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* BrumeProcessor::createEditor() { return new BrumeEditor (*this); }

#if ! BRUME_TEST
#ifndef BRUME_EMBEDDED
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BrumeProcessor(); }
#endif
#endif
