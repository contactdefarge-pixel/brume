// Offline test for Brume Station: levels, CPU, MIDI-clock sync, the snare build, step editing,
// editor snapshots and a short demo WAV.
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Station/StationProcessor.h"
#include "../Station/StationEditor.h"

using namespace station;

static void setP (StationProcessor& p, const char* pid, float v)
{
    if (auto* prm = p.apvts.getParameter (pid)) prm->setValueNotifyingHost (v);
}

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

static Meter render (StationProcessor& p, double seconds, juce::AudioBuffer<float>* record = nullptr, double* cpu = nullptr,
                     double clockBpm = 0.0)
{
    Meter m;
    juce::AudioBuffer<float> buf (2, bs);
    juce::MidiBuffer midi;
    const int blocks = (int) (seconds * fs / bs);
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

static void press (StationProcessor& p, int note) { p.sendControl (juce::MidiMessage::noteOn (16, note, (juce::uint8) 127)); p.sendControl (juce::MidiMessage::noteOff (16, note)); }

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    StationProcessor p;
    p.prepareToPlay (fs, bs);

    std::printf ("silence (all off)    : %6.1f dB\n", render (p, 1.0).rmsDb());

    setP (p, id::patOn, 1.0f);
    auto m = render (p, 6.0);
    std::printf ("patterns             : %6.1f dB rms, %5.1f dB peak, step %d\n", m.rmsDb(), m.peakDb(), p.getPlayStep());
    setP (p, id::patOn, 0.0f); render (p, 1.0);

    setP (p, id::snareOn, 1.0f); setP (p, id::snareRate, 0.6f);
    m = render (p, 4.0);
    std::printf ("snare 1/16           : %6.1f dB rms, %5.1f dB peak\n", m.rmsDb(), m.peakDb());
    setP (p, id::snareOn, 0.0f); render (p, 1.0);

    setP (p, id::padOn, 1.0f);
    m = render (p, 6.0);
    std::printf ("pad                  : %6.1f dB rms, %5.1f dB peak\n", m.rmsDb(), m.peakDb());
    setP (p, id::padOn, 0.0f); render (p, 4.0);

    setP (p, id::brumeOn, 1.0f);
    m = render (p, 4.0);
    std::printf ("brume noise          : %6.1f dB rms, %5.1f dB peak\n", m.rmsDb(), m.peakDb());

    setP (p, id::patOn, 1.0f); setP (p, id::snareOn, 1.0f); setP (p, id::padOn, 1.0f);
    double cpu = 0;
    m = render (p, 8.0, nullptr, &cpu);
    std::printf ("everything           : %6.1f dB rms, %5.1f dB peak, CPU %.1f %% of one core\n", m.rmsDb(), m.peakDb(), cpu);

    press (p, 13);   // DROP
    render (p, 0.1);
    std::printf ("after DROP           : columns off = %s\n",
                 p.apvts.getRawParameterValue (id::patOn)->load() < 0.5f && p.apvts.getRawParameterValue (id::snareOn)->load() < 0.5f ? "yes" : "NO");
    press (p, 14);   // KILL
    m = render (p, 1.0);
    std::printf ("after KILL           : %6.1f dB\n", m.rmsDb());

    // build 2 bars at 124 BPM = 3.87 s, quantised to the next bar
    press (p, 1);
    render (p, 0.05);
    float maxLeft = -1.0f; int hits = 0;
    for (int i = 0; i < 120; ++i) { render (p, 0.05); maxLeft = std::max (maxLeft, p.getBuildBeatsLeft()); hits += p.getBuildBeatsLeft() >= 0 ? 1 : 0; }
    std::printf ("build                : started with %.1f beats left, ran %.1f s\n", maxLeft, hits * 0.05);
    std::printf ("build after 6 s      : %s\n", p.getBuildBeatsLeft() < 0 ? "finished on the bar" : "STILL RUNNING");

    // MIDI clock from "Traktor" at 128 BPM
    render (p, 2.0, nullptr, nullptr, 128.0);
    std::printf ("MIDI clock           : source %d, %.2f BPM (expect 1, 128)\n", p.getSyncSource(), p.getBpm());
    render (p, 1.0);

    // step editor: TYPE, pad 3 on the kick
    const bool before = p.stepIsOn (p.getSlot(), 0, 2);
    press (p, 26); press (p, 3);
    render (p, 0.05);
    std::printf ("step edit            : mode %s, kick step 3 %d -> %d\n", p.isStepMode() ? "STEP" : "PERFORM", (int) before, (int) p.stepIsOn (p.getSlot(), 0, 2));
    press (p, 3); render (p, 0.05);

    // knobs from the F1: CC 2 = snare speed, SHIFT + CC 2 = snare level
    p.sendControl (juce::MidiMessage::controllerEvent (16, 2, 127));
    p.sendControl (juce::MidiMessage::noteOn (16, 24, (juce::uint8) 127));
    p.sendControl (juce::MidiMessage::controllerEvent (16, 2, 0));
    p.sendControl (juce::MidiMessage::noteOff (16, 24));
    render (p, 0.05);
    std::printf ("F1 knob 2            : speed %.2f (expect 1), SHIFT level %.2f (expect 0)\n",
                 p.apvts.getRawParameterValue (id::snareRate)->load(), p.apvts.getRawParameterValue (id::snareLevel)->load());
    setP (p, id::snareLevel, 0.8f);
    p.sendControl (juce::MidiMessage::controllerEvent (16, 9, 1)); render (p, 0.05);
    std::printf ("encoder in STEP      : track %d (expect 1)\n", p.getSelectedTrack());

    // editor snapshots
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
        save ("station-step.png");
        press (p, 26); render (p, 0.05);
        setP (p, id::patOn, 1.0f); setP (p, id::brumeOn, 1.0f); setP (p, id::padOn, 1.0f);
        render (p, 2.0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        save ("station-perform.png");
    }

    // demo: patterns + pad, then a 4-bar snare build into the drop, impact + crash, the tail rings out
    {
        StationProcessor d;
        d.prepareToPlay (fs, bs);
        juce::AudioBuffer<float> rec (2, 0);
        setP (d, id::bpm, d.apvts.getParameter (id::bpm)->convertTo0to1 (124.0f));
        setP (d, id::patOn, 1.0f); setP (d, id::padOn, 1.0f); setP (d, id::padOpen, 0.35f);
        render (d, 7.74 * 1.0, &rec);                 // ~ 4 bars
        setP (d, id::brumeOn, 1.0f);
        press (d, 2);                                  // BUILD 4
        for (int i = 0; i < 8; ++i) { setP (d, id::padOpen, 0.35f + 0.08f * i); render (d, 1.0, &rec); }
        press (d, 13);                                 // DROP
        press (d, 5); press (d, 6);                    // impact + crash
        render (d, 0.1, &rec);
        setP (d, id::patOn, 1.0f); setP (d, id::padChord, 5.0f / 6.0f); setP (d, id::padOn, 1.0f);
        render (d, 8.0, &rec);
        juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile ("station-demo.wav");
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), fs, 2, 24, {}, 0));
        if (w) w->writeFromAudioSampleBuffer (rec, 0, rec.getNumSamples());
        std::printf ("demo                 : %s (%.1f s)\n", out.getFullPathName().toRawUTF8(), rec.getNumSamples() / fs);
    }
    return 0;
}
