#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include "F1Reader.h"
#include "Reverb.h"

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
    // embedded = used inside Brume Pad (no F1 reader, no timer); revoloNoise = noise voiced like the Teile Revolo
    explicit BrumeProcessor (bool embedded = false);
    void setRevoloNoise (bool r) { revoloNoise = r; }
    void setNoiseTrim (float g)    { noiseTrim = g; }   // embedded use: overall noise level
    // embedded use: play a recorded noise (e.g. the real Revolo) instead of the generator; call before prepareToPlay
    void setNoiseSample (const float* l, const float* r, int length, double sampleRate)
    {
        rawNoiseL.assign (l, l + length); rawNoiseR.assign (r, r + length); rawNoiseRate = sampleRate;
    }
    void setSlowNoiseDown (bool b) { slowNoiseDown = b; }
    void setFreeTremolo (bool b)   { freeTremolo = b; }   // tremolo always in Hz, never locked to the beat
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
    // MIDI monitor: last control message received (any input), for troubleshooting controllers
    int  getMidiActivity() const      { return midiActivity.load(); }
    int  getLastMidiCode() const      { return lastMidiCode.load(); }
    int  getLastMidiValue() const     { return lastMidiValue.load(); }
    int  getMidiMapping (int t) const { return midiMap[(size_t) t].load(); }
    void clearMidiMapping (int t)     { midiMap[(size_t) t].store (-1); }
    static juce::String describeMapping (int code);

    // ---- recorder: writes what comes out of Brume to ~/Music/Brume/*.wav ----
    bool startRecording();
    void stopRecording();
    bool isRecording() const          { return activeWriter.load() != nullptr; }
    double getRecordedSeconds() const { return (double) recSamples.load() / std::max (1.0, fs); }
    juce::File getLastRecording() const { return lastRecording; }

    // ---- Traktor Kontrol F1 read directly over USB (standalone app) ----
    F1Reader::Status getF1Status() const { return f1Reader.getStatus(); }

    // ---- tempo sync ----
    // 0 = free (Tempo knob in Hz), 1 = MIDI clock (e.g. Traktor), 2 = host tempo (e.g. Ableton)
    int   getSyncSource() const { return syncSource.load(); }
    float getSyncBpm() const    { return syncBpm.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
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

    // reverbs: algorithmic, every parameter glides (no cut when turning Length / Character)
    brume::FdnReverb fdn, fdnNoise;
    bool prepared = false;

    using Filter = juce::dsp::IIR::Filter<float>;
    Filter preHP[2], airShelf[2], lowCut[2], noiseHP[2], noiseLP[2];
    float lastPreHP = -1, lastAir = -1, lastNoiseSetup = -1;
    float lowCutSmoothed = 160.0f, lastLowCut = -1;

    juce::AudioBuffer<float> sendBuf, noiseBuf, washBuf;

    // smoothing state
    float sendGate = 0.0f, noiseGate = 0.0f, dryGain = 1.0f, sendAmt = 0.0f, noiseAmt = 0.0f, tremDepth = 0.0f;
    int tremDivIdx = -1; bool freeTremolo = false;
    double tremPhase = 0.0;
    std::atomic<float> tremPhaseUi { 0.0f };
    std::atomic<bool> tapReset { false };
    juce::Random noiseRandom;
    bool revoloNoise = false, slowNoiseDown = false;
    float noiseTrim = 1.0f;
    // recorded noise, played by two overlapping grains from random places (no audible loop)
    std::vector<float> rawNoiseL, rawNoiseR, nsL, nsR;
    double rawNoiseRate = 44100.0;
    struct Grain { double pos = 0.0; int age = 0; };
    Grain grains[2];
    int grainLen = 4096;
    std::vector<float> grainWin;
    inline void noiseFromSample (float& l, float& r);

    // tap tempo (message thread)
    juce::Array<double> taps;

    // MIDI learn state. Mapping code = type << 16 | channel << 8 | number (type 1 = CC, 2 = note), -1 = none.
    std::array<std::atomic<int>, numMidiTargets> midiMap;
    std::array<int, numMidiTargets> midiLast {};
    std::atomic<int> learnTarget { -1 }, learnedTarget { -1 };
    std::atomic<int> midiActivity { 0 }, lastMidiCode { -1 }, lastMidiValue { 0 };

    // F1 direct: the reader thread queues MIDI-like messages that processBlock picks up
    juce::MidiMessageCollector f1Collector;
    std::atomic<bool> f1Ready { false };
    juce::MidiBuffer f1Midi;
    F1Reader f1Reader { [this] (const juce::MidiMessage& m)
    {
        if (! f1Ready.load()) return;
        auto msg = m;
        msg.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
        f1Collector.addMessageToQueue (msg);
    } };

    // recorder
    juce::TimeSliceThread recThread { "Brume recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> recWriter;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
    juce::CriticalSection recLock;
    std::atomic<juce::int64> recSamples { 0 };
    juce::File lastRecording;
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
