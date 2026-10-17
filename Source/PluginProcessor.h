#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace brume
{
    // Parameter mappings, shared by the DSP, the host display and the editor.
    inline float lengthSeconds (float v)  { return 0.3f * std::pow (40.0f, v); }        // 0.3 s .. 12 s
    inline float tremoloRateHz (float v)  { return 0.2f * std::pow (200.0f, v); }       // 0.2 .. 40 Hz
    inline float lowCutHz (float v)       { return 20.0f * std::pow (1000.0f, v); }     // 20 Hz .. 20 kHz
    inline float tempoFromRate (float hz) { return std::log (hz / 0.2f) / std::log (200.0f); }

    // When synced (MIDI clock or host tempo), the Tempo knob picks a note division instead of a rate.
    struct Division { double cyclesPerBeat; const char* label; };
    inline const Division& division (float v)
    {
        static const Division d[] = { { 0.25, "1 bar" }, { 0.5, "1/2" }, { 1.0, "1/4" }, { 1.5, "1/4T" }, { 2.0, "1/8" },
                                      { 3.0, "1/8T" }, { 4.0, "1/16" }, { 6.0, "1/16T" }, { 8.0, "1/32" }, { 16.0, "1/64" } };
        const int i = std::max (0, std::min (9, (int) std::lround (v * 9.0f)));
        return d[i];
    }

    namespace id
    {
        static constexpr const char* reverb    = "reverb";
        static constexpr const char* character = "character";
        static constexpr const char* length    = "length";
        static constexpr const char* air       = "air";
        static constexpr const char* noise     = "noise";
        static constexpr const char* tremolo   = "tremolo";
        static constexpr const char* tempo     = "tempo";
        static constexpr const char* filter    = "filter";
        static constexpr const char* wet       = "wet";
        static constexpr const char* on        = "on";
    }
}

class BrumeProcessor : public juce::AudioProcessor,
                       private juce::Timer
{
public:
    BrumeProcessor();
    ~BrumeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Brume"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 14.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Called by the editor's TAP footswitch: one tap = one tremolo cycle.
    void tap();

    // Tremolo phase (0..1), read by the editor to blink the TAP LEDs in sync.
    float getTremoloPhase() const { return tremPhaseUi.load(); }

    // ---- MIDI learn ----
    // Targets: 0..7 = the knobs (reverb, character, length, air, noise, tremolo, tempo, filter),
    // 8 = Mix/Wet, 9 = On/Off, 10 = Tap.
    static constexpr int numMidiTargets = 11;
    static const char* midiTargetName (int t);
    void setLearnTarget (int t)       { learnTarget.store (t); }
    int  getLearnTarget() const       { return learnTarget.load(); }
    int  popLearnedTarget()           { return learnedTarget.exchange (-1); }
    int  getMidiMapping (int t) const { return midiMap[(size_t) t].load(); }
    void clearMidiMapping (int t)     { midiMap[(size_t) t].store (-1); }
    static juce::String describeMapping (int code);

    // ---- tempo sync ----
    // 0 = free (Tempo knob in Hz), 1 = MIDI clock (e.g. Traktor), 2 = host tempo (e.g. Ableton)
    int   getSyncSource() const { return syncSource.load(); }
    float getSyncBpm() const    { return syncBpm.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void rebuildImpulseResponse();
    void updateFilters (bool force);
    void handleMidi (const juce::MidiMessage&, juce::int64 samplePos);
    void processChunk (juce::AudioBuffer<float>&, int syncSrc, double bpm, double beatAtStart);
    void tapAtSample (juce::int64 samplePos);
    void setParamFromMidi (const char* paramId, float normalised);

    // parameters
    std::atomic<float>* pReverb = nullptr;  std::atomic<float>* pCharacter = nullptr;
    std::atomic<float>* pLength = nullptr;  std::atomic<float>* pAir = nullptr;
    std::atomic<float>* pNoise = nullptr;   std::atomic<float>* pTremolo = nullptr;
    std::atomic<float>* pTempo = nullptr;   std::atomic<float>* pFilter = nullptr;
    std::atomic<float>* pWet = nullptr;     std::atomic<float>* pOn = nullptr;

    double fs = 44100.0;
    int maxBlock = 512;

    // reverb: one convolution for the input, one for the noise wash (same IR)
    juce::dsp::Convolution reverbConv { juce::dsp::Convolution::NonUniform { 512 } };
    juce::dsp::Convolution noiseConv  { juce::dsp::Convolution::NonUniform { 512 } };
    float irLength = -1.0f, irCharacter = -1.0f;
    float pendingLength = -1.0f, pendingCharacter = -1.0f;
    int pendingTicks = 0;
    juce::Random irRandom;
    bool prepared = false;

    using Filter = juce::dsp::IIR::Filter<float>;
    Filter preHP[2], airShelf[2], lowCut[2], noiseHP[2], noiseLP[2];
    float lastPreHP = -1, lastAir = -1, lastNoiseSetup = -1;
    float lowCutSmoothed = 160.0f, lastLowCut = -1;

    juce::AudioBuffer<float> sendBuf, noiseBuf, washBuf;

    // smoothing state
    float sendGate = 0.0f, noiseGate = 0.0f, dryGain = 1.0f, sendAmt = 0.0f, noiseAmt = 0.0f, tremDepth = 0.0f;
    double tremPhase = 0.0;
    std::atomic<float> tremPhaseUi { 0.0f };
    std::atomic<bool> tapReset { false };
    juce::Random noiseRandom;

    // tap tempo (message thread)
    juce::Array<double> taps;

    // MIDI learn state. Mapping code = type << 16 | channel << 8 | number (type 1 = CC, 2 = note), -1 = none.
    std::array<std::atomic<int>, numMidiTargets> midiMap;
    std::array<int, numMidiTargets> midiLast {};
    std::atomic<int> learnTarget { -1 }, learnedTarget { -1 };
    juce::int64 sampleCounter = 0;
    std::array<juce::int64, 8> midiTaps {};

    // tempo sync state (audio thread)
    double clockTickSamples = 0.0, clockTicks = 0.0, beatPos = 0.0;
    juce::int64 lastClockSample = -1;
    std::atomic<int> syncSource { 0 };
    std::atomic<float> syncBpm { 0.0f };
    int numMidiTaps = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrumeProcessor)
};
