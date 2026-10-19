// Offline test for Brume Station: levels, CPU, MIDI-clock sync, programs, rolls, builds, step editing,
// editor snapshots and a short demo WAV.
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Station/StationProcessor.h"
#include "../Station/StationEditor.h"

using namespace station;

static void setP (StationProcessor& p, const char* pid, float v)
{
    if (auto* prm = p.apvts.getParameter (pid)) prm->setValueNotifyingHost (v);
}
static float getP (StationProcessor& p, const char* pid) { return p.apvts.getRawParameterValue (pid)->load(); }

struct Meter { double sumSq = 0; juce::int64 n = 0; float peak = 0; void add (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < 2; ++c) { auto* d = b.getReadPointer (c); for (int i = 0; i < b.getNumSamples(); ++i) { sumSq += d[i] * d[i]; peak = std::max (peak, std::abs (d[i])); } }
    n += 2 * b.getNumSamples();
}
    double rmsDb() const { return n ? 10.0 * std::log10 (sumSq / (double) n + 1e-20) : -200.0; }
    double peakDb() const { return 20.0 * std::log10 (peak + 1e-20); }
};

static const double fs = 48000.0;
static const int bs = 512;

static Meter render (StationProcessor& p, double seconds, juce::AudioBuffer<float>* record = nullptr, double* cpu = nullptr, double clockBpm = 0.0)
{
    Meter m;
    juce::AudioBuffer<float> buf (2, bs);
    juce::MidiBuffer midi;
    const int blocks = std::max (1, (int) std::lround (seconds * fs / bs));
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    static double clockPhase = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (clockBpm > 0.0)
        {
            const double tick = fs * 60.0 / (clockBpm * 24.0);
            while (clockPhase < bs) { midi.addEvent (juce::MidiMessage::midiClock(), (int) clockPhase); clockPhase += tick; }
            clockPhase -= bs;
        }
        p.processBlock (buf, midi);
        m.add (buf);
        if (record != nullptr)
        {
            const int start = record->getNumSamples();
            record->setSize (2, start + bs, true, true, true);
            for (int c = 0; c < 2; ++c) record->copyFrom (c, start, buf, c, 0, bs);
        }
    }
    if (cpu != nullptr) *cpu = (juce::Time::getMillisecondCounterHiRes() - t0) / (seconds * 1000.0) * 100.0;
    return m;
}

static void down (StationProcessor& p, int note) { p.sendControl (juce::MidiMessage::noteOn (16, note, (juce::uint8) 127)); }
static void up (StationProcessor& p, int note)   { p.sendControl (juce::MidiMessage::noteOff (16, note)); }
static void press (StationProcessor& p, int note) { down (p, note); up (p, note); }
static void selectProgram (StationProcessor& p, int prog) { down (p, 24); press (p, prog + 1); up (p, 24); render (p, 0.02); }

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    StationProcessor p;
    p.prepareToPlay (fs, bs);

    std::printf ("silence (all off)    : %6.1f dB\n", render (p, 1.0).rmsDb());

    // programs
    selectProgram (p, progPatterns);
    std::printf ("SHIFT + pad 3        : program %d (expect %d) display %s\n", p.getProgram(), progPatterns, p.getDisplayText().toRawUTF8());

    // ROLL: hold the 1/16 pad (pad 8) for 2 s on the snare
    selectProgram (p, progRoll);
    press (p, 1);   // SNARE
    render (p, 0.5);
    down (p, 8);
    auto m = render (p, 2.0);
    up (p, 8);
    std::printf ("roll 1/16 snare      : %6.1f dB rms, %5.1f dB peak\n", m.rmsDb(), m.peakDb());
    m = render (p, 1.0);
    std::printf ("after release        : %6.1f dB (tail only)\n", m.rmsDb());

    // HOLD latches the roll
    press (p, 23); press (p, 7); render (p, 1.0);
    std::printf ("HOLD + 1/8           : latched %s\n", p.isHoldOn() ? "yes" : "NO");
    press (p, 7); press (p, 23); render (p, 1.0);

    // patterns
    setP (p, id::patOn, 1.0f);
    m = render (p, 6.0);
    std::printf ("patterns             : %6.1f dB rms, %5.1f dB peak, step %d\n", m.rmsDb(), m.peakDb(), p.getPlayStep());

    // pad: pressing a chord pad in the PAD program starts it
    selectProgram (p, progPad);
    press (p, 5);   // degree V
    m = render (p, 5.0);
    std::printf ("pad + patterns       : %6.1f dB rms, chord %.2f (expect 0.57), pad on %.0f\n", m.rmsDb(), getP (p, id::padChord), getP (p, id::padOn));

    setP (p, id::brumeOn, 1.0f);
    double cpu = 0;
    m = render (p, 8.0, nullptr, &cpu);
    std::printf ("everything           : %6.1f dB rms, %5.1f dB peak, CPU %.1f %% of one core\n", m.rmsDb(), m.peakDb(), cpu);

    // knobs follow the program: knob 1 in PAD = OPEN, SHIFT + knob 1 = WIDTH
    p.sendControl (juce::MidiMessage::controllerEvent (16, 1, 127));
    down (p, 24); p.sendControl (juce::MidiMessage::controllerEvent (16, 1, 0)); up (p, 24);
    render (p, 0.05);
    std::printf ("PAD knob 1           : open %.2f (expect 1), SHIFT width %.2f (expect 0)\n", getP (p, id::padOpen), getP (p, id::padWidth));
    p.sendControl (juce::MidiMessage::controllerEvent (16, 5, 0)); render (p, 0.05);
    std::printf ("fader 1              : brume volume %.2f (expect 0)\n", getP (p, id::brumeLevel));
    setP (p, id::brumeLevel, 0.5f);

    // DROP (TYPE) and KILL
    press (p, 26); render (p, 0.1);
    std::printf ("DROP                 : patterns %.0f pad %.0f brume %.0f (expect 0 0 0)\n", getP (p, id::patOn), getP (p, id::padOn), getP (p, id::brumeOn));
    selectProgram (p, progFx);
    press (p, 10); m = render (p, 1.0);
    std::printf ("KILL                 : %6.1f dB\n", m.rmsDb());

    // build from ROLL (pad 13 = BUILD 2)
    selectProgram (p, progRoll);
    press (p, 13);
    float maxLeft = -1.0f; double ran = 0.0;
    for (int i = 0; i < 140; ++i) { render (p, bs / fs * 4); maxLeft = std::max (maxLeft, p.getBuildBeatsLeft()); ran += p.getBuildBeatsLeft() >= 0 ? bs * 4 / fs : 0.0; }
    std::printf ("build 2 bars         : started with %.1f beats left, ran %.2f s (8 beats at 124 = 3.87 s)\n", maxLeft, ran);

    render (p, 2.0, nullptr, nullptr, 128.0);
    std::printf ("MIDI clock           : source %d, %.2f BPM (expect 1, 128)\n", p.getSyncSource(), p.getBpm());
    render (p, 1.0);

    // PATTERNS step edit
    selectProgram (p, progPatterns);
    const bool before = p.stepIsOn (p.getSlot(), 0, 2);
    press (p, 3); render (p, 0.05);
    std::printf ("step edit            : kick step 3 %d -> %d\n", (int) before, (int) p.stepIsOn (p.getSlot(), 0, 2));
    press (p, 3);
    p.sendControl (juce::MidiMessage::controllerEvent (16, 9, 1)); render (p, 0.05);
    std::printf ("encoder in PATTERNS  : track %d (expect 1), display %s\n", p.getSelectedTrack(), p.getDisplayText().toRawUTF8());

    // editor snapshots + wiki
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        auto save = [&] (const char* name)
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (name);
            out.deleteFile();
            juce::FileOutputStream os (out);
            juce::PNGImageFormat().writeImageToStream (img, os);
            std::printf ("snapshot             : %s\n", out.getFullPathName().toRawUTF8());
        };
        setP (p, id::patOn, 1.0f); render (p, 2.0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
        save ("station-patterns.png");
        selectProgram (p, progRoll); down (p, 8); render (p, 0.5);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
        save ("station-roll.png");
        up (p, 8); render (p, 0.1);
        down (p, 24); render (p, 0.05);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
        save ("station-shift.png");
        up (p, 24); render (p, 0.05);
        if (auto* help = ed->findChildWithID ("dummy")) juce::ignoreUnused (help);
        for (auto* c : ed->getChildren())
            if (auto* b = dynamic_cast<juce::Component*> (c)) juce::ignoreUnused (b);
    }

    // demo: groove + pad, a held snare roll that speeds up and climbs, then a 4-bar build, drop with impact
    {
        StationProcessor d;
        d.prepareToPlay (fs, bs);
        juce::AudioBuffer<float> rec (2, 0);
        setP (d, id::patOn, 1.0f); setP (d, id::padOn, 1.0f); setP (d, id::padOpen, 0.35f);
        render (d, 7.74, &rec);
        selectProgram (d, progRoll);
        down (d, 6); render (d, 1.94, &rec); up (d, 6);       // 1/4
        down (d, 7); render (d, 1.94, &rec); up (d, 7);       // 1/8
        down (d, 8);
        for (int i = 0; i < 8; ++i) { setP (d, id::rollPitch, 0.5f + 0.03f * i); setP (d, id::rollFilter, 0.5f + 0.03f * i); render (d, 0.242, &rec); }
        up (d, 8);
        down (d, 9); render (d, 1.94, &rec); up (d, 9);       // 1/32
        setP (d, id::rollPitch, 0.5f); setP (d, id::rollFilter, 0.5f);
        setP (d, id::brumeOn, 1.0f);
        press (d, 14);                                         // BUILD 4
        for (int i = 0; i < 8; ++i) { setP (d, id::padOpen, 0.35f + 0.08f * i); render (d, 1.0, &rec); }
        press (d, 26);                                         // DROP
        selectProgram (d, progFx); press (d, 1); press (d, 2); // impact + crash
        render (d, 0.1, &rec);
        setP (d, id::patOn, 1.0f); setP (d, id::padChord, 5.0f / 7.0f); setP (d, id::padOn, 1.0f);
        render (d, 8.0, &rec);
        juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile ("station-demo.wav");
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), fs, 2, 24, {}, 0));
        if (w) w->writeFromAudioSampleBuffer (rec, 0, rec.getNumSamples());
        std::printf ("demo                 : %s (%.1f s)\n", out.getFullPathName().toRawUTF8(), rec.getNumSamples() / fs);

        // the snare alone, for listening
        StationProcessor s;
        s.prepareToPlay (fs, bs);
        juce::AudioBuffer<float> sn (2, 0);
        selectProgram (s, progRoll);
        for (int i = 0; i < 4; ++i) { press (s, 1); render (s, 0.484, &sn); }
        down (s, 8); render (s, 1.94, &sn); up (s, 8); render (s, 1.0, &sn);
        juce::File out2 = juce::File::getCurrentWorkingDirectory().getChildFile ("station-snare.wav");
        out2.deleteFile();
        std::unique_ptr<juce::AudioFormatWriter> w2 (wav.createWriterFor (new juce::FileOutputStream (out2), fs, 2, 24, {}, 0));
        if (w2) w2->writeFromAudioSampleBuffer (sn, 0, sn.getNumSamples());
    }
    return 0;
}
