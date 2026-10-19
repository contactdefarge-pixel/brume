#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include "../Source/F1Reader.h"
#include "../Source/Reverb.h"
#include "StationDSP.h"

namespace station
{
    namespace id
    {
        static constexpr const char* brumeOn = "brume_on";   static constexpr const char* brumeAmount = "brume_amount";
        static constexpr const char* brumeLength = "brume_length"; static constexpr const char* brumeSend = "brume_send";
        static constexpr const char* brumeChar = "brume_char";
        static constexpr const char* snareOn = "snare_on";   static constexpr const char* snareRate = "snare_rate";
        static constexpr const char* snarePitch = "snare_pitch"; static constexpr const char* snareLevel = "snare_level";
        static constexpr const char* snareTone = "snare_tone";
        static constexpr const char* patOn = "pat_on";       static constexpr const char* patVolume = "pat_volume";
        static constexpr const char* patDensity = "pat_density"; static constexpr const char* patKit = "pat_kit";
        static constexpr const char* padOn = "pad_on";       static constexpr const char* padOpen = "pad_open";
        static constexpr const char* padChord = "pad_chord"; static constexpr const char* padTimbre = "pad_timbre";
        static constexpr const char* key = "key";
        static constexpr const char* quant = "quant";        static constexpr const char* bpm = "bpm";
        static constexpr const char* master = "master";
    }

    // Snare speeds, in hits per beat
    struct Rate { float perBeat; const char* label; };
    inline const Rate& snareRate (float v)
    {
        static const Rate r[] = { { 1.0f, "1/4" }, { 2.0f, "1/8" }, { 3.0f, "1/8T" }, { 4.0f, "1/16" }, { 6.0f, "1/16T" }, { 8.0f, "1/32" } };
        return r[juce::jlimit (0, 5, (int) std::lround (v * 5.0f))];
    }
    inline float snareSemis (float v) { return -12.0f + 36.0f * v; }    // -12 .. +24 semitones
    inline int chordDegree (float v)  { return juce::jlimit (0, 6, (int) std::lround (v * 6.0f)); }

    constexpr int numTracks = 5, numSteps = 16, numSlots = 4;
    inline const char* trackName (int t) { static const char* n[] = { "KICK", "CLAP", "HAT", "OPEN HAT", "PERC" }; return n[juce::jlimit (0, 4, t)]; }

    // Pad meaning in PERFORM mode (pad index 0..15, top-left first)
    inline const char* performPadName (int p)
    {
        static const char* n[] = { "BUILD 2", "BUILD 4", "BUILD 8", "BUILD 16",
                                   "IMPACT", "CRASH", "CLAP", "SWELL",
                                   "CHORD 1", "CHORD 4", "CHORD 5", "CHORD 6",
                                   "DROP", "KILL", "TAP", "SWELL 2" };
        return n[juce::jlimit (0, 15, p)];
    }
}

class StationProcessor : public juce::AudioProcessor,
                         private juce::Timer
{
public:
    StationProcessor();
    ~StationProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Brume Station"; }
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

    juce::AudioProcessorValueTreeState apvts;

    // Same messages as the F1 (channel 16): notes 1-16 pads, 17-20 mutes, 21 SYNC, 22 QUANT, 24 SHIFT,
    // 25 REVERSE, 26 TYPE, 28 BROWSE, 29 encoder push; CC 1-4 knobs, 5-8 faders, 9 encoder (1 / 127).
    // The editor uses this too, so a mouse click does exactly what the controller does.
    void sendControl (const juce::MidiMessage& m);

    // ---- state for the editor and the F1 lights (message thread) ----
    F1Reader::Status getF1Status() const { return f1Reader.getStatus(); }
    int   getSyncSource() const   { return syncSource.load(); }   // 0 internal, 1 MIDI clock, 2 host
    float getBpm() const          { return bpmUi.load(); }
    float getBarBeat() const      { return barBeatUi.load(); }   // 0..4
    bool  isStepMode() const      { return stepMode.load(); }
    bool  isShiftHeld() const     { return shiftHeld.load(); }
    int   getSelectedTrack() const { return selTrack.load(); }
    int   getSlot() const         { return slot.load(); }
    int   getPlayStep() const     { return playStep.load(); }
    bool  isArmed (int column) const { return armed[(size_t) column].load(); }
    float getBuildBeatsLeft() const  { return buildLeftUi.load(); }   // < 0 = no build
    bool  stepIsOn (int s, int t, int step) const { return (pattern[(size_t) s][(size_t) t].load() >> step) & 1; }
    juce::uint32 getPadColour (int p) const { return padRgb[(size_t) p].load(); }   // 0xRRGGBB, as on the F1
    juce::String getDisplayText() const;
    juce::String getLastEvent() const;
    float padFlash (int p) const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void handleMidi (const juce::MidiMessage&, juce::int64 samplePos);
    void handleControl (int type, int num, int val);
    void pressPad (int p);
    void schedule (double atBarBeat, int action, int arg = 0);
    void runAction (int action, int arg);
    void stepTick (int step);
    void triggerDrum (int track, float vel);
    void triggerSnare (float vel, float extraSemis);
    void setParam (const char* paramId, float normalised);
    void toggleParam (const char* paramId);
    double nextBeat (double tolerance = 0.12) const;
    double nextBar (double tolerance = 0.25) const;
    void buildLeds();
    void loadPreset (int s);

    std::atomic<float>* p (const char* pid) { return apvts.getRawParameterValue (pid); }
    std::atomic<float> *pBrumeOn, *pBrumeAmount, *pBrumeLength, *pBrumeSend, *pBrumeChar,
                       *pSnareOn, *pSnareRate, *pSnarePitch, *pSnareLevel, *pSnareTone,
                       *pPatOn, *pPatVolume, *pPatDensity, *pPatKit,
                       *pPadOn, *pPadOpen, *pPadChord, *pPadTimbre, *pKey, *pQuant, *pBpm, *pMaster;

    double fs = 44100.0;
    int maxBlock = 512;
    juce::int64 sampleCounter = 0;

    // voices
    std::array<station::SnareVoice, 8> snares; int nextSnare = 0;
    std::array<station::DrumVoice, 12> drums;  int nextDrum = 0;
    std::array<station::OneShot, 6> shots;     int nextShot = 0;
    station::PadSynth pad;
    brume::FdnReverb reverb;
    juce::AudioBuffer<float> sendBuf;
    station::Rng noiseRng;
    station::Svf noiseHP[2], noiseLP[2];
    float noiseGate = 0, padGate = 0, sendSm = 0.5f, masterSm = 0.8f;
    int lastKey = -1, lastDegree = -1;

    // clock
    double beat = 0.0, barOffset = 0.0;
    double clockTickSamples = 0.0, clockTicks = 0.0;
    juce::int64 lastClockSample = -1;
    juce::int64 lastStepIdx = -1, lastSnareIdx = -1;
    float lastRatePerBeat = 0.0f;
    std::atomic<int> syncSource { 0 };
    std::atomic<float> bpmUi { 124.0f }, barBeatUi { 0.0f }, buildLeftUi { -1.0f };
    juce::Array<double> taps;

    // columns (previous on/off state, to start them on the grid)
    bool prevOn[4] = { false, false, false, false };
    bool running[4] = { false, false, false, false };   // brume, snare, patterns, pad actually playing
    std::array<std::atomic<bool>, 4> armed {};

    // automatic snare build
    bool buildActive = false;
    double buildStart = 0.0, buildLen = 16.0;
    std::atomic<bool> buildEnded { false };

    // scheduled actions (quantised)
    struct Pending { double at; int action, arg; bool used; };
    std::array<Pending, 32> pending {};

    // patterns: [slot][track] = 16-bit step mask
    std::array<std::array<std::atomic<int>, station::numTracks>, station::numSlots> pattern {};
    float hashTable[station::numTracks][station::numSteps] {};
    std::atomic<int> playStep { -1 }, selTrack { 0 }, slot { 0 };
    std::atomic<bool> stepMode { false }, shiftHeld { false };

    // lights and feedback
    std::array<std::atomic<juce::uint32>, 16> padRgb {};
    std::array<std::atomic<double>, 16> padHitTime {};
    std::atomic<int> lastEventCode { -1 };
    double uiTime() const { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    // controller input (F1 over USB + editor clicks)
    juce::MidiMessageCollector controlQueue;
    std::atomic<bool> queueReady { false };
    F1Reader f1Reader { [this] (const juce::MidiMessage& m) { sendControl (m); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StationProcessor)
};
