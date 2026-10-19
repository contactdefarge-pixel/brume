#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include "../Source/F1Reader.h"
#include "../Source/Reverb.h"
#include "StationDSP.h"

// Brume Station works in PROGRAMS. SHIFT + a pad of the top row picks the program the F1 controls
// (SHIFT + pad 5 = FX). The 4 faders (volumes) and the 4 STOP buttons (on/off) stay global, so
// programs keep playing and can be mixed whichever one is on screen.
namespace station
{
    enum Program { progBrume = 0, progRoll, progPatterns, progPad, progFx, numPrograms };
    inline const char* programName (int p) { static const char* n[] = { "BRUME", "ROLL", "PATTERNS", "PAD", "FX" }; return n[juce::jlimit (0, 4, p)]; }
    inline juce::uint32 programColour (int p) { static const juce::uint32 c[] = { 0xc8dcff, 0xff8a1f, 0xff3030, 0x40a0ff, 0xb050ff }; return c[juce::jlimit (0, 4, p)]; }

    namespace id
    {
        // global
        static constexpr const char* brumeOn = "brume_on";   static constexpr const char* rollOn = "roll_on";
        static constexpr const char* patOn = "pat_on";       static constexpr const char* padOn = "pad_on";
        static constexpr const char* brumeLevel = "brume_level"; static constexpr const char* rollLevel = "roll_level";
        static constexpr const char* patLevel = "pat_level"; static constexpr const char* padLevel = "pad_level";
        static constexpr const char* key = "key";            static constexpr const char* quant = "quant";
        static constexpr const char* bpm = "bpm";            static constexpr const char* master = "master";
        // BRUME
        static constexpr const char* brumeLength = "brume_length"; static constexpr const char* brumeChar = "brume_char";
        static constexpr const char* brumeAir = "brume_air";       static constexpr const char* brumeTrem = "brume_trem";
        static constexpr const char* brumeTremRate = "brume_trem_rate"; static constexpr const char* brumeLowCut = "brume_lowcut";
        static constexpr const char* brumeSend = "brume_send";     static constexpr const char* brumeColor = "brume_color";
        // ROLL
        static constexpr const char* rollPitch = "roll_pitch";     static constexpr const char* rollDecay = "roll_decay";
        static constexpr const char* rollFilter = "roll_filter";   static constexpr const char* rollSend = "roll_send";
        static constexpr const char* rollTone = "roll_tone";       static constexpr const char* rollSwing = "roll_swing";
        static constexpr const char* rollPan = "roll_pan";         static constexpr const char* rollDrive = "roll_drive";
        static constexpr const char* rollSound = "roll_sound";
        // PATTERNS
        static constexpr const char* patDensity = "pat_density";   static constexpr const char* patKit = "pat_kit";
        static constexpr const char* patSwing = "pat_swing";       static constexpr const char* patFilter = "pat_filter";
        static constexpr const char* patDecay = "pat_decay";       static constexpr const char* patDrive = "pat_drive";
        static constexpr const char* patSend = "pat_send";         static constexpr const char* patAccent = "pat_accent";
        // PAD
        static constexpr const char* padChord = "pad_chord";       static constexpr const char* padOpen = "pad_open";
        static constexpr const char* padTimbre = "pad_timbre";     static constexpr const char* padAttack = "pad_attack";
        static constexpr const char* padRelease = "pad_release";   static constexpr const char* padWidth = "pad_width";
        static constexpr const char* padSend = "pad_send";         static constexpr const char* padArpRate = "pad_arp_rate";
        static constexpr const char* padOctave = "pad_octave";     static constexpr const char* padArp = "pad_arp";
        static constexpr const char* padSus = "pad_sus";
        // FX
        static constexpr const char* fxLevel = "fx_level";         static constexpr const char* fxSend = "fx_send";
        static constexpr const char* fxTone = "fx_tone";           static constexpr const char* fxLength = "fx_length";
    }

    // the 4 knobs of each program (and their SHIFT functions)
    struct KnobDef { const char* pid; const char* label; };
    inline const KnobDef& knobDef (int prog, int knob, bool shift)
    {
        static const KnobDef d[numPrograms][2][4] = {
            { { { id::brumeLength, "LENGTH" }, { id::brumeChar, "CHARACTER" }, { id::brumeAir, "AIR" }, { id::brumeTrem, "TREMOLO" } },
              { { id::brumeTremRate, "TREM RATE" }, { id::brumeLowCut, "LOW CUT" }, { id::brumeSend, "SEND" }, { id::brumeColor, "NOISE COLOR" } } },
            { { { id::rollPitch, "PITCH" }, { id::rollDecay, "DECAY" }, { id::rollFilter, "FILTER" }, { id::rollSend, "REVERB" } },
              { { id::rollTone, "TONE" }, { id::rollSwing, "SWING" }, { id::rollPan, "PAN" }, { id::rollDrive, "DRIVE" } } },
            { { { id::patDensity, "DENSITY" }, { id::patKit, "TUNE" }, { id::patSwing, "SWING" }, { id::patFilter, "FILTER" } },
              { { id::patDecay, "DECAY" }, { id::patDrive, "DRIVE" }, { id::patSend, "REVERB" }, { id::patAccent, "ACCENT" } } },
            { { { id::padOpen, "OPEN" }, { id::padTimbre, "TIMBRE" }, { id::padAttack, "ATTACK" }, { id::padRelease, "RELEASE" } },
              { { id::padWidth, "WIDTH" }, { id::padSend, "REVERB" }, { id::padArpRate, "ARP RATE" }, { id::padOctave, "OCTAVE" } } },
            { { { id::fxLevel, "LEVEL" }, { id::fxSend, "REVERB" }, { id::fxTone, "TONE" }, { id::fxLength, "RISER LEN" } },
              { { id::fxLevel, "LEVEL" }, { id::fxSend, "REVERB" }, { id::fxTone, "TONE" }, { id::fxLength, "RISER LEN" } } } };
        return d[juce::jlimit (0, numPrograms - 1, prog)][shift ? 1 : 0][juce::jlimit (0, 3, knob)];
    }

    // divisions in hits (or cycles) per beat
    struct Div { float perBeat; const char* label; };
    inline const Div& rollDiv (int i)
    {
        static const Div d[] = { { 0.5f, "1/2" }, { 1.0f, "1/4" }, { 2.0f, "1/8" }, { 4.0f, "1/16" },
                                 { 8.0f, "1/32" }, { 3.0f, "1/8T" }, { 6.0f, "1/16T" }, { 16.0f, "1/64" } };
        return d[juce::jlimit (0, 7, i)];
    }
    inline const Div& tremDiv (float v)
    {
        static const Div d[] = { { 0.5f, "1/2" }, { 1.0f, "1/4" }, { 2.0f, "1/8" }, { 3.0f, "1/8T" }, { 4.0f, "1/16" }, { 6.0f, "1/16T" }, { 8.0f, "1/32" } };
        return d[juce::jlimit (0, 6, (int) std::lround (v * 6.0f))];
    }
    inline const Div& arpDiv (float v)
    {
        static const Div d[] = { { 2.0f, "1/8" }, { 3.0f, "1/8T" }, { 4.0f, "1/16" }, { 6.0f, "1/16T" } };
        return d[juce::jlimit (0, 3, (int) std::lround (v * 3.0f))];
    }
    inline float lengthSeconds (float v) { return 0.3f * std::pow (40.0f, v); }
    inline float lengthNorm (float s)    { return std::log (s / 0.3f) / std::log (40.0f); }
    inline int   chordDegree (float v)   { return juce::jlimit (0, 7, (int) std::lround (v * 7.0f)); }
    inline int   octaveOf (float v)      { return juce::jlimit (-1, 1, (int) std::lround (v * 2.0f) - 1); }
    inline int   soundOf (float v)       { return juce::jlimit (0, 3, (int) std::lround (v * 3.0f)); }
    inline float rollSemis (float v)     { return -12.0f + 24.0f * v; }
    inline const char* rollSoundName (int s) { static const char* n[] = { "SNARE", "CLAP", "KICK", "HAT" }; return n[juce::jlimit (0, 3, s)]; }
    inline int   riserBars (float v)     { static const int b[] = { 2, 4, 8, 16 }; return b[juce::jlimit (0, 3, (int) std::lround (v * 3.0f))]; }

    constexpr int numTracks = 5, numSteps = 16, numSlots = 4;
    inline const char* trackName (int t) { static const char* n[] = { "KICK", "CLAP", "HAT", "OPEN HAT", "PERC" }; return n[juce::jlimit (0, 4, t)]; }
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

    // Same messages as the F1 (channel 16): notes 1-16 pads, 17-20 STOP, 21 SYNC, 22 QUANT, 23 CAPTURE (HOLD),
    // 24 SHIFT, 25 REVERSE, 26 TYPE (DROP), 27 SIZE (next program), 28 BROWSE (tap), 29 encoder push;
    // CC 1-4 knobs, 5-8 faders, 9 encoder (1 / 127). The editor uses this too.
    void sendControl (const juce::MidiMessage& m);

    // ---- state for the editor and the F1 lights (message thread) ----
    F1Reader::Status getF1Status() const { return f1Reader.getStatus(); }
    int   getProgram() const      { return program.load(); }
    int   getSyncSource() const   { return syncSource.load(); }
    float getBpm() const          { return bpmUi.load(); }
    float getBarBeat() const      { return barBeatUi.load(); }
    bool  isShiftHeld() const     { return shiftHeld.load(); }
    bool  isHoldOn() const        { return holdMode.load(); }
    int   getSelectedTrack() const { return selTrack.load(); }
    int   getSlot() const         { return slot.load(); }
    int   getPlayStep() const     { return playStep.load(); }
    bool  isArmed (int column) const { return armed[(size_t) column].load(); }
    float getBuildBeatsLeft() const  { return buildLeftUi.load(); }
    bool  stepIsOn (int s, int t, int step) const { return (pattern[(size_t) s][(size_t) t].load() >> step) & 1; }
    juce::uint32 getPadColour (int p) const { return padRgb[(size_t) p].load(); }
    juce::String getPadLabel (int p) const;
    juce::String getDisplayText() const;
    juce::String getLastEvent() const;
    float padFlash (int p) const;
    bool  padHeld (int p) const { return padDown[(size_t) p].load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void handleMidi (const juce::MidiMessage&, juce::int64 samplePos);
    void handleControl (int type, int num, int val);
    void pressPad (int p, bool down);
    void schedule (double atBarBeat, int action, int arg = 0);
    void runAction (int action, int arg);
    void stepTick (int step);
    void triggerKit (int sound, float semis, float vel, float gate, float pan, int bus);
    void setParam (const char* paramId, float normalised);
    void toggleParam (const char* paramId);
    double nextBeat (double tolerance = 0.12) const;
    double nextBar (double tolerance = 0.25) const;
    void buildLeds();
    void loadPreset (int s);
    int  activeRollDiv() const;

    std::atomic<float>* p (const char* pid) { return apvts.getRawParameterValue (pid); }
    float v (const char* pid) const { return apvts.getRawParameterValue (pid)->load(); }

    double fs = 44100.0;
    int maxBlock = 512;
    juce::int64 sampleCounter = 0;

    // sound sources
    station::Kit kit;
    std::array<station::SampleVoice, 10> rollVoices; int nextRoll = 0;
    std::array<station::SampleVoice, 12> drumVoices; int nextDrumV = 0;
    std::array<station::DrumVoice, 4> percVoices;     int nextPerc = 0;
    std::array<station::OneShot, 8> shots;            int nextShot = 0;
    std::array<station::ArpVoice, 3> arps;            int nextArp = 0;
    station::PadSynth pad;
    brume::FdnReverb reverb;
    juce::AudioBuffer<float> sendBuf;
    station::Rng noiseRng;
    station::Svf noiseHP[2], noiseLP[2], brumeCut[2], rollFilt[2], patFilt[2], fxFilt[2];
    float noiseGate = 0, sendSm = 0.5f, masterSm = 0.8f, killFade = 1.0f;
    int lastKey = -1, lastDegree = -1, lastOct = 99; bool lastSus = false;
    int arpStep = 0; juce::int64 lastArpIdx = -1;

    // clock
    double beat = 0.0, barOffset = 0.0;
    double clockTickSamples = 0.0, clockTicks = 0.0;
    juce::int64 lastClockSample = -1;
    juce::int64 lastStepIdx = -1, lastRollIdx = -1;
    float lastRollRate = 0.0f;
    std::atomic<int> syncSource { 0 };
    std::atomic<float> bpmUi { 124.0f }, barBeatUi { 0.0f }, buildLeftUi { -1.0f };
    juce::Array<double> taps;

    // programs on/off (STOP buttons), started on the grid
    bool prevOn[4] = { false, false, false, false };
    bool running[4] = { false, false, false, false };
    std::array<std::atomic<bool>, 4> armed {};

    // ROLL: pads held (or latched with HOLD) set the roll division; automatic builds
    std::array<bool, 8> rollHeld {};
    std::array<int, 8> rollOrder {}; int rollCounter = 0;
    bool buildActive = false;
    double buildStart = 0.0, buildLen = 16.0;

    // BRUME momentary pads
    bool freezeHeld = false, throwHeld = false, noiseHeld = false;

    // scheduled actions (quantised)
    struct Pending { double at; int action, arg; bool used; };
    std::array<Pending, 32> pending {};

    // patterns: [slot][track] = 16-bit step mask
    std::array<std::array<std::atomic<int>, station::numTracks>, station::numSlots> pattern {};
    float hashTable[station::numTracks][station::numSteps] {};
    std::atomic<int> playStep { -1 }, selTrack { 0 }, slot { 0 }, program { station::progRoll };
    std::atomic<bool> shiftHeld { false }, holdMode { false };

    // lights and feedback
    std::array<std::atomic<juce::uint32>, 16> padRgb {};
    std::array<std::atomic<double>, 16> padHitTime {};
    std::array<std::atomic<bool>, 16> padDown {};
    std::atomic<int> lastEventCode { -1 };
    std::atomic<double> encoderTime { -10.0 };
    double uiTime() const { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    juce::MidiMessageCollector controlQueue;
    std::atomic<bool> queueReady { false };
    F1Reader f1Reader { [this] (const juce::MidiMessage& m) { sendControl (m); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StationProcessor)
};
