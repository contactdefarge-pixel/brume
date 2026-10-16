// Offline test: renders signals through BrumeProcessor, prints levels, and saves a snapshot of the editor.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <chrono>
#include <thread>

static void setParam (BrumeProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

static double rmsDb (const juce::AudioBuffer<float>& b)
{
    double s = 0; long n = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) { s += (double) b.getSample (c, i) * b.getSample (c, i); ++n; }
    return 10.0 * std::log10 (s / n + 1e-20);
}

// render `seconds`, input = white noise at `inDb` RMS (or silence), measuring the last `measure` seconds
static double render (BrumeProcessor& p, double fs, float seconds, float inDb, float measure, double* cpuMsPerBlock = nullptr)
{
    const int bs = 512;
    const int total = (int) (seconds * fs), meas = (int) (measure * fs);
    juce::AudioBuffer<float> blk (2, bs), acc (2, meas);
    juce::MidiBuffer midi;
    juce::Random r (1);
    const float amp = inDb > -200 ? juce::Decibels::decibelsToGain (inDb) * 1.7320508f : 0.0f;  // uniform: rms = amp/sqrt(3)
    int done = 0, accPos = 0; double cpu = 0; int blocks = 0;
    while (done < total)
    {
        const int n = std::min (bs, total - done);
        blk.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) blk.setSample (c, i, (r.nextFloat() * 2 - 1) * amp);
        auto t0 = std::chrono::high_resolution_clock::now();
        p.processBlock (blk, midi);
        cpu += std::chrono::duration<double, std::milli> (std::chrono::high_resolution_clock::now() - t0).count(); ++blocks;
        for (int i = 0; i < n; ++i)
        {
            const int pos = done + i - (total - meas);
            if (pos >= 0) { acc.setSample (0, pos, blk.getSample (0, i)); acc.setSample (1, pos, blk.getSample (1, i)); }
        }
        done += n;
    }
    if (cpuMsPerBlock) *cpuMsPerBlock = cpu / blocks;
    juce::ignoreUnused (accPos);
    return rmsDb (acc);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double fs = 48000.0;
    BrumeProcessor p;
    p.setPlayConfigDetails (2, 2, fs, 512);
    p.prepareToPlay (fs, 512);
    std::this_thread::sleep_for (std::chrono::milliseconds (800));   // let the IR load
    render (p, fs, 0.5f, -300, 0.1f);
    std::this_thread::sleep_for (std::chrono::milliseconds (300));

    double cpu = 0;
    setParam (p, brume::id::on, 0);
    std::printf ("dry (off)            : %6.1f dB\n", render (p, fs, 2, -16, 1));
    setParam (p, brume::id::noise, 0); setParam (p, brume::id::on, 1);
    std::printf ("on, reverb, no noise : %6.1f dB\n", render (p, fs, 5, -16, 2, &cpu));
    std::printf ("cpu per 512 block    : %6.3f ms (budget %.1f ms)\n", cpu, 512.0 / fs * 1000.0);
    setParam (p, brume::id::wet, 1);
    std::printf ("wet only (reverb)    : %6.1f dB\n", render (p, fs, 4, -16, 2));
    setParam (p, brume::id::wet, 0);
    setParam (p, brume::id::tremolo, 0); setParam (p, brume::id::noise, 1);
    std::printf ("noise alone max      : %6.1f dB  (target about -16)\n", render (p, fs, 4, -300, 2));
    setParam (p, brume::id::noise, 0.5f);
    std::printf ("noise alone 0.5      : %6.1f dB\n", render (p, fs, 4, -300, 2));
    // off: the tail must fade, not stop
    setParam (p, brume::id::noise, 1);
    render (p, fs, 3, -16, 0.1f);
    setParam (p, brume::id::on, 0);
    std::printf ("tail 0-0.25 s after off (silent in): %6.1f dB\n", render (p, fs, 0.25f, -300, 0.25f));
    std::printf ("tail 1.0-1.25 s                  : %6.1f dB\n", (render (p, fs, 0.75f, -300, 0.1f), render (p, fs, 0.25f, -300, 0.25f)));
    std::printf ("tail 3.0-3.25 s                  : %6.1f dB\n", (render (p, fs, 1.75f, -300, 0.1f), render (p, fs, 0.25f, -300, 0.25f)));
    std::printf ("tail 8 s                         : %6.1f dB\n", (render (p, fs, 4.75f, -300, 0.1f), render (p, fs, 0.25f, -300, 0.25f)));

    // length change at runtime (IR rebuild via timer) must not crash
    setParam (p, brume::id::length, 1.0f);
    for (int i = 0; i < 20; ++i) { juce::MessageManager::getInstance()->runDispatchLoopUntil (20); render (p, fs, 0.05f, -16, 0.01f); }
    std::printf ("after length=max     : ok\n");

    // MIDI learn
    {
        auto sendMidi = [&] (const juce::MidiMessage& m)
        {
            juce::AudioBuffer<float> blk (2, 480); blk.clear();
            juce::MidiBuffer mb; mb.addEvent (m, 0);
            p.processBlock (blk, mb);
        };
        auto norm = [&] (const char* id) { return p.apvts.getParameter (id)->getValue(); };
        p.setLearnTarget (4);                                   // NOISE
        sendMidi (juce::MidiMessage::controllerEvent (1, 21, 10));
        std::printf ("learned NOISE        : %s\n", BrumeProcessor::describeMapping (p.getMidiMapping (4)).toRawUTF8());
        sendMidi (juce::MidiMessage::controllerEvent (1, 21, 127));
        std::printf ("CC21=127 -> noise    : %.2f (expect 1.00)\n", norm (brume::id::noise));
        sendMidi (juce::MidiMessage::controllerEvent (1, 21, 0));
        std::printf ("CC21=0   -> noise    : %.2f (expect 0.00)\n", norm (brume::id::noise));
        p.setLearnTarget (9);                                   // ON/OFF on a note
        sendMidi (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100));
        sendMidi (juce::MidiMessage::noteOff (1, 36));
        const float before = norm (brume::id::on);
        sendMidi (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100)); sendMidi (juce::MidiMessage::noteOff (1, 36));
        std::printf ("note 36 toggles on   : %.0f -> %.0f\n", before, norm (brume::id::on));
        p.setLearnTarget (10);                                  // TAP
        sendMidi (juce::MidiMessage::controllerEvent (1, 40, 127)); sendMidi (juce::MidiMessage::controllerEvent (1, 40, 0));
        for (int k = 0; k < 4; ++k)
        {
            for (int b = 0; b < 49; ++b) sendMidi (juce::MidiMessage::controllerEvent (2, 99, 0));   // 49 * 480 samples ~ 0.49 s
            sendMidi (juce::MidiMessage::controllerEvent (1, 40, 127)); sendMidi (juce::MidiMessage::controllerEvent (1, 40, 0));
        }
        std::printf ("tap every 0.5 s      : %.2f Hz (expect ~2)\n", brume::tremoloRateHz (p.apvts.getRawParameterValue (brume::id::tempo)->load()));
        juce::MemoryBlock mb; p.getStateInformation (mb);
        BrumeProcessor p2; p2.setStateInformation (mb.getData(), (int) mb.getSize());
        std::printf ("state restore map    : %s / %s\n", BrumeProcessor::describeMapping (p2.getMidiMapping (4)).toRawUTF8(), BrumeProcessor::describeMapping (p2.getMidiMapping (10)).toRawUTF8());
    }

    // editor snapshot
    setParam (p, brume::id::on, 1);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile ("editor.png");
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("editor snapshot      : %s\n", out.getFullPathName().toRawUTF8());
    {
        auto* be = dynamic_cast<BrumeEditor*> (ed.get());
        be->testLearnMode (true);
        p.setLearnTarget (6);
        auto img2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File out2 = juce::File::getCurrentWorkingDirectory().getChildFile ("editor-learn.png");
        out2.deleteFile(); juce::FileOutputStream os2 (out2);
        juce::PNGImageFormat().writeImageToStream (img2, os2);
        p.setLearnTarget (-1);
    }
    ed.reset();
    return 0;
}
