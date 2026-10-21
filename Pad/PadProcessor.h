#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include "../Source/PluginProcessor.h"
#include "../Source/Reverb.h"
#include "../Station/StationDSP.h"
#include "PadFX.h"
#if BRUME_HAS_LINK
 #include <ableton/Link.hpp>
#endif

// Brume Pad: an Akai LPD8 (MK2) turned into an RMX-1000 style performance box.
//   Program 1 = X-PAD drums (top pads: KICK SNARE CLAP HAT, bottom pads: roll speed) + one-knob effects
//   Program 2 = Brume (white noise voiced like the Revolo, reverb, tremolo)
// The music from Traktor goes through (BlackHole -> Brume Pad -> speakers). KILL FX stops every effect,
// leaving the echo and reverb tails to ring out.
namespace bpad
{
    constexpr int numPads = 8, numKnobs = 8, numProgs = 4;

    namespace id
    {
        static constexpr const char* drums = "p1_drums";   static constexpr const char* pitch = "p1_pitch";
        static constexpr const char* delay = "p1_delay";   static constexpr const char* reverb = "p1_reverb";
        static constexpr const char* hpfEcho = "p1_hpf_echo"; static constexpr const char* lpfEcho = "p1_lpf_echo";
        static constexpr const char* crushEcho = "p1_crush_echo"; static constexpr const char* noise = "p1_noise";
        static constexpr const char* sound = "p1_sound";
        static constexpr const char* bpm = "bpm";          static constexpr const char* master = "master";
    }
    inline const char* p1KnobId (int k)
    {
        static const char* ids[] = { id::drums, id::pitch, id::delay, id::reverb, id::hpfEcho, id::lpfEcho, id::crushEcho, id::noise };
        return ids[juce::jlimit (0, 7, k)];
    }
    // Brume parameters on the knobs of program 2 (same order as the pedal)
    inline const char* p2KnobId (int k)
    {
        static const char* ids[] = { brume::id::reverb, brume::id::character, brume::id::length, brume::id::air,
                                     brume::id::noise, brume::id::tremolo, brume::id::tempo, brume::id::filter };
        return ids[juce::jlimit (0, 7, k)];
    }
    inline const char* knobLabel (int prog, int k)
    {
        static const char* p1[] = { "DRUMS VOL", "PITCH", "DELAY S/L", "REVERB", "HPF ECHO", "LPF ECHO", "CRUSH ECHO", "WHITE NOISE" };
        static const char* p2[] = { "REVERB", "CHARACTER", "LENGTH", "AIR", "NOISE", "TREMOLO", "TEMPO", "FILTER" };
        if (prog == 0) return p1[k];
        if (prog == 1) return p2[k];
        return "-";
    }
    // pad numbers as printed on the LPD8: 1-4 bottom row, 5-8 top row
    inline const char* padLabel (int prog, int pad)
    {
        static const char* p1[] = { "1/4", "1/8", "1/16", "1/32", "KICK", "SNARE", "CLAP", "HAT" };
        static const char* p2[] = { "ON / OFF", "TAP", "TREM ON", "HOLD", "TREM 1/4", "TREM 1/8", "TREM 1/16", "TREM 1/32" };
        if (prog == 0) return p1[pad];
        if (prog == 1) return p2[pad];
        return "-";
    }
    inline float rollPerBeat (int pad) { static const float r[] = { 1.0f, 2.0f, 4.0f, 8.0f }; return r[juce::jlimit (0, 3, pad)]; }
    inline const char* soundName (int s) { static const char* n[] = { "KICK", "SNARE", "CLAP", "HAT" }; return n[juce::jlimit (0, 3, s)]; }
}

class PadProcessor : public juce::AudioProcessor,
                     private juce::Timer,
                     private juce::MidiInputCallback
{
public:
    PadProcessor();
    ~PadProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Brume Pad"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    BrumeProcessor brumeEngine { true };   // program 2

    // ---- from the editor (message thread) ----
    void padFromScreen (int pad, bool down, int prog);
    void knobFromScreen (int knob, float v, int prog);
    void killFx()                     { killRequest.store (true); }
    void selectProgram (int p)        { program.store (juce::jlimit (0, bpad::numProgs - 1, p)); }
    void startLearn()                 { learnStep.store (1); }
    void startLink();   // standalone does it by itself; tests call it
    void cancelLearn()                { learnStep.store (0); }

    // ---- state for the editor ----
    int   getProgram() const          { return program.load(); }
    int   getLearnStep() const        { return learnStep.load(); }   // 0 none, 1 P1 pad, 2 P1 knob, 3 P2 pad, 4 P2 knob
    bool  isLearned (int p) const     { return maps[(size_t) p].learned; }
    int   getSyncSource() const       { return syncSource.load(); }   // 0 internal, 1 MIDI clock, 2 host, 3 Ableton Link
    int   getLinkPeers() const        { return linkPeers.load(); }
    float getBpm() const              { return bpmUi.load(); }
    float getBarBeat() const          { return barBeatUi.load(); }
    bool  padIsDown (int p) const     { return padDown[(size_t) p].load(); }
    float padFlash (int p) const;
    float getKnobValue (int prog, int k) const;
    juce::String getKnobText (int prog, int k) const;
    bool  isFxKilled (int k) const    { return fxKilledUi[(size_t) k].load(); }
    float getKillFlash() const;
    juce::String getMidiStatus() const;
    juce::String getLastEvent() const { const juce::ScopedLock sl (eventLock); return lastEvent; }
    bool  isBrumeOn() const           { return brumeEngine.apvts.getRawParameterValue (brume::id::on)->load() > 0.5f; }
    int   getSelectedSound() const    { return (int) std::lround (apvts.getRawParameterValue (bpad::id::sound)->load() * 3.0f); }
    int   getActiveSound() const      { return activeSound.load(); }
    int   getActiveSpeed() const      { return activeSpeed.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handleMidi (const juce::MidiMessage&, juce::int64 samplePos);
    void handlePad (int prog, int pad, bool down, float velocity);
    void handleKnob (int prog, int knob, float v);
    void doKill();
    void setParam (juce::AudioProcessorValueTreeState& t, const char* pid, float v);
    void triggerSound (int sound, float vel, float gateSeconds = 10.0f);
    void setEvent (const juce::String& s) { const juce::ScopedLock sl (eventLock); lastEvent = s; }

    // LPD8 mapping, learned once per program (PROGRAM + pad on the LPD8 changes the notes / CCs it sends)
    struct ProgMap { bool learned = false; int padChannel = 0, padNote0 = 36, knobChannel = 0, knobCC0 = 70; };
    std::array<ProgMap, bpad::numProgs> maps;
    std::atomic<int> learnStep { 0 }, program { 0 };

    double fs = 44100.0;
    int maxBlock = 512;
    juce::int64 sampleCounter = 0;

    // drums (program 1)
    pad::RmxKit kit;
    std::array<station::SampleVoice, 12> voices; int nextVoice = 0;
    std::atomic<int> activeSound { -1 }, activeSpeed { 0 };   // latched: sound playing (-1 = none) and speed pad
    int activeBeforeCombo = -1;
    juce::int64 lastRollIdx = -1; float lastRollRate = 0.0f;
    double lastPad5Time = -10.0, lastPad8Time = -10.0;
    float soundBeforeCombo = 1.0f / 3.0f;

    // effects (program 1)
    std::array<pad::FxKnob, 8> fx;   // indices = knobs; 0 drums and 1 pitch are not effects
    pad::Echo delayFx, hpfEcho, lpfEcho, crushEcho;
    station::Svf dryHp[2], dryLp[2];
    pad::Crusher dryCrush;
    brume::FdnReverb reverbFx, noiseWash;
    pad::RevoloNoise noiseGen;
    juce::AudioBuffer<float> revBuf, washBuf;
    float sm[8] {};          // smoothed effect amounts
    float hpHz = 20.0f, lpHz = 20000.0f;
    std::array<std::atomic<bool>, 8> fxKilledUi {};

    // clock
    double beat = 0.0, clockTickSamples = 0.0, clockTicks = 0.0;
    juce::int64 lastClockSample = -1;
    std::atomic<int> syncSource { 0 };
    std::atomic<float> bpmUi { 124.0f }, barBeatUi { 0.0f };
    juce::Array<double> taps;
    juce::MidiBuffer clockForBrume;
    std::atomic<int> linkPeers { 0 };
   #if BRUME_HAS_LINK
    std::unique_ptr<ableton::Link> link;
   #endif

    // feedback
    std::array<std::atomic<bool>, 8> padDown {};
    std::array<std::atomic<double>, 8> padHit {};
    std::atomic<double> killTime { -10.0 };
    std::atomic<bool> killRequest { false };
    juce::CriticalSection eventLock;
    juce::String lastEvent;
    double uiTime() const { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    // MIDI: in the standalone app, Brume Pad opens the LPD8 and Traktor's clock port itself
    juce::MidiMessageCollector queue;
    std::atomic<bool> queueReady { false };
    juce::OwnedArray<juce::MidiInput> openInputs;
    juce::StringArray openNames;
    std::atomic<bool> lpd8Seen { false }, traktorSeen { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadProcessor)
};
