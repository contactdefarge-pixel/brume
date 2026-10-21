// Offline test for Brume Pad: LPD8 mapping and setup, X-PAD rolls, effects + KILL tails, Brume program,
// MIDI clock, editor snapshots and a demo over a synthetic track.
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Pad/PadProcessor.h"
#include "../Pad/PadEditor.h"

using namespace bpad;
static const double fs = 48000.0;
static const int bs = 512;

struct Meter { double sumSq = 0; juce::int64 n = 0; float peak = 0;
    void add (const juce::AudioBuffer<float>& b) { for (int c = 0; c < 2; ++c) { auto* d = b.getReadPointer (c); for (int i = 0; i < b.getNumSamples(); ++i) { sumSq += d[i] * d[i]; peak = std::max (peak, std::abs (d[i])); } } n += 2 * b.getNumSamples(); }
    double rmsDb() const { return n ? 10.0 * std::log10 (sumSq / (double) n + 1e-20) : -200.0; }
    double peakDb() const { return 20.0 * std::log10 (peak + 1e-20); } };

// a small house loop at 124 BPM so the effects have something to chew on
struct Track
{
    double t = 0.0; bool on = true; juce::Random rnd { 3 };
    void fill (juce::AudioBuffer<float>& b)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float y = 0.0f;
            if (on)
            {
                const double beatLen = 60.0 / 124.0, tb = std::fmod (t, beatLen);
                y += (float) (std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 + 120.0 * std::exp (-tb / 0.03)) * tb) * std::exp (-tb / 0.25)) * 0.5f;
                const double off = std::fmod (t + beatLen * 0.5, beatLen);
                y += (rnd.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-off / 0.03) * 0.12f;
                const double chord[] = { 220.0, 261.6, 329.6 };
                for (double f : chord) y += (float) (2.0 * std::fmod (t * f, 1.0) - 1.0) * 0.035f;
            }
            b.setSample (0, i, y); b.setSample (1, i, y);
            t += 1.0 / fs;
        }
    }
};

struct Rig
{
    PadProcessor p; Track track; juce::MidiBuffer pending; double clockBpm = 0.0, clockPhase = 0.0;
    juce::AudioBuffer<float> rec { 2, 0 }; bool recording = false;
    Rig() { p.prepareToPlay (fs, bs); }
    Meter run (double seconds, double* cpu = nullptr)
    {
        Meter m; juce::AudioBuffer<float> buf (2, bs); juce::MidiBuffer midi;
        const int blocks = std::max (1, (int) std::lround (seconds * fs / bs));
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < blocks; ++b)
        {
            track.fill (buf);
            midi.clear();
            if (b == 0) { midi.addEvents (pending, 0, -1, 0); pending.clear(); }
            if (clockBpm > 0.0)
            {
                const double tick = fs * 60.0 / (clockBpm * 24.0);
                while (clockPhase < bs) { midi.addEvent (juce::MidiMessage::midiClock(), (int) clockPhase); clockPhase += tick; }
                clockPhase -= bs;
            }
            p.processBlock (buf, midi);
            m.add (buf);
            if (recording) { const int s = rec.getNumSamples(); rec.setSize (2, s + bs, true, true, true); for (int c = 0; c < 2; ++c) rec.copyFrom (c, s, buf, c, 0, bs); }
        }
        if (cpu) *cpu = (juce::Time::getMillisecondCounterHiRes() - t0) / (seconds * 1000.0) * 100.0;
        return m;
    }
    void note (int ch, int n, bool on) { pending.addEvent (on ? juce::MidiMessage::noteOn (ch, n, (juce::uint8) 100) : juce::MidiMessage::noteOff (ch, n), 0); }
    void cc (int ch, int n, int v) { pending.addEvent (juce::MidiMessage::controllerEvent (ch, n, v), 0); }
    float par (const char* pid) { return p.apvts.getRawParameterValue (pid)->load(); }
};

static void saveWav (const juce::AudioBuffer<float>& b, const char* name)
{
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (name);
    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), fs, 2, 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    std::printf ("wav                  : %s (%.1f s)\n", out.getFullPathName().toRawUTF8(), b.getNumSamples() / fs);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    {
        Rig r;
        std::printf ("music through        : %6.1f dB\n", r.run (2.0).rmsDb());

        // program 1 (factory LPD8 MK2: pads 36-43, knobs CC 70-77): SNARE (pad 6) then hold 1/16 (pad 3)
        r.note (10, 41, true); r.note (10, 41, false); r.run (0.5);
        std::printf ("pad 6 (one press)    : %s playing %s (expect SNARE at 1/4)\n", soundName (r.p.getSelectedSound()),
                     r.p.getActiveSound() == 1 ? padLabel (0, r.p.getActiveSpeed()) : "NOTHING");
        r.note (10, 38, true); r.note (10, 38, false); auto m = r.run (2.0);
        std::printf ("1/16 (one press)     : speed %s, %6.1f dB rms, %5.1f dB peak\n", padLabel (0, r.p.getActiveSpeed()), m.rmsDb(), m.peakDb());
        r.note (10, 41, true); r.note (10, 41, false); r.run (0.5);
        std::printf ("pad 6 again          : playing %s (expect none)\n", r.p.getActiveSound() < 0 ? "none" : "STILL");
        r.note (10, 36, true); r.note (10, 36, false);   // back to 1/4 for the next tests
        r.run (1.0);

        // effects: each knob, then KILL with pads 5 + 8; the tail must keep ringing when the music stops
        for (int k = 2; k < 8; ++k)
        {
            r.cc (10, 70 + k, 100); m = r.run (2.0);
            std::printf ("K%d %-15s : %6.1f dB rms, %5.1f dB peak\n", k + 1, knobLabel (0, k), m.rmsDb(), m.peakDb());
            r.cc (10, 70 + k, 0); r.run (1.0);
        }
        r.cc (10, 72, 110); r.cc (10, 73, 100); r.cc (10, 74, 90); r.run (2.0);
        double cpu = 0; r.run (4.0, &cpu);
        std::printf ("CPU (3 effects + music): %.1f %% of one core\n", cpu);
        r.track.on = false;
        r.note (10, 40, true); r.note (10, 43, true); r.note (10, 40, false); r.note (10, 43, false);
        const auto m1 = r.run (0.5), m2 = r.run (1.5), m3 = r.run (6.0);
        std::printf ("KILL (pads 5+8)      : tail %5.1f dB (0-0.5 s), %5.1f dB (0.5-2 s), %5.1f dB (2-8 s), K3 killed %s\n",
                     m1.rmsDb(), m2.rmsDb(), m3.rmsDb(), r.p.isFxKilled (2) ? "yes" : "NO");
        r.cc (10, 72, 60); r.run (0.1);
        std::printf ("K3 moved again       : killed %s (expect no)\n", r.p.isFxKilled (2) ? "YES" : "no");
        r.cc (10, 16, 100); r.run (0.1);   // a pad in CC mode
        std::printf ("CC-mode pad          : K3 killed %s (expect yes)\n", r.p.isFxKilled (2) ? "yes" : "NO");
        r.track.on = true;

        // LPD8 setup: program 1 on channel 10, program 2 on channel 11 (different notes / CCs)
        r.p.startLearn();
        r.note (10, 36, true); r.run (0.05); r.cc (10, 70, 5); r.run (0.05);
        r.note (11, 44, true); r.run (0.05); r.cc (11, 80, 5); r.run (0.05);
        std::printf ("setup                : step %d, P1 %s, P2 %s\n", r.p.getLearnStep(), r.p.isLearned (0) ? "ok" : "NO", r.p.isLearned (1) ? "ok" : "NO");
        r.note (11, 44, true); r.note (11, 44, false); r.run (0.3);   // PAD 1 of program 2 = Brume ON
        std::printf ("program detection    : program %d (expect 1), Brume on %s\n", r.p.getProgram() + 1 - 1, r.p.isBrumeOn() ? "yes" : "NO");
        r.cc (11, 84, 110); m = r.run (3.0);   // K5 = NOISE
        std::printf ("Brume (Revolo noise) : %6.1f dB rms\n", m.rmsDb());
        r.note (10, 36, true); r.note (10, 36, false); r.run (0.05);
        std::printf ("back to program 1    : program %d (expect 0)\n", r.p.getProgram());

        r.clockBpm = 128.0; r.run (2.0);
        std::printf ("MIDI clock           : source %d, %.2f BPM (expect 1, 128)\n", r.p.getSyncSource(), r.p.getBpm());
        r.clockBpm = 0.0; r.run (1.0);
       #if BRUME_HAS_LINK
        {
            // a second Link peer playing 132 BPM, like Traktor with LINK on
            ableton::Link peer (132.0);
            peer.enable (true);
            r.p.startLink();
            for (int i = 0; i < 40 && r.p.getLinkPeers() == 0; ++i) { juce::Thread::sleep (100); r.run (0.05); }
            r.run (1.0);
            std::printf ("Ableton Link         : peers %d, source %d, %.2f BPM (expect >=1, 3, 132)\n", r.p.getLinkPeers(), r.p.getSyncSource(), r.p.getBpm());
            peer.enable (false);
        }
       #endif

        // snapshots
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.p.createEditor());
        ed->setVisible (true);
        auto snap = [&] (const char* name)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (name);
            out.deleteFile(); juce::FileOutputStream os (out); juce::PNGImageFormat().writeImageToStream (img, os);
            std::printf ("snapshot             : %s\n", out.getFullPathName().toRawUTF8());
        };
        r.cc (10, 74, 70); r.cc (10, 76, 40); r.note (10, 38, true); r.note (10, 38, false); r.note (10, 41, true); r.note (10, 41, false); r.run (0.3);
        snap ("pad-p1.png");
        r.note (10, 41, true); r.note (10, 41, false); r.note (11, 45, true); r.note (11, 45, false); r.run (0.3);
        snap ("pad-p2.png");
    }

    // demo: track, snare build with speed changes, K1 and K2 up, drop: K1 down + HPF echo throw, reverb, crush,
    // long delay, Revolo noise, KILL FX (tails ring out)
    {
        Rig r; r.recording = true;
        auto knob = [&] (int k, float v) { r.cc (10, 70 + k, juce::roundToInt (v * 127.0f)); };
        r.run (7.74);
        r.note (10, 41, true); r.note (10, 41, false);           // SNARE on (1/4)
        knob (0, 0.35f); knob (1, 0.5f);
        const int speeds[] = { 36, 37, 38, 39 };
        for (int s = 0; s < 4; ++s)
        {
            if (s > 0) { r.note (10, speeds[s], true); r.note (10, speeds[s], false); }
            for (int i = 0; i < 4; ++i) { knob (0, 0.35f + 0.05f * (s * 4 + i)); knob (1, 0.5f + 0.012f * (s * 4 + i)); r.run (0.484); }
        }
        r.note (10, 41, true); r.note (10, 41, false);           // SNARE off = drop
        knob (0, 0.8f); knob (1, 0.5f);
        knob (4, 0.55f); r.run (1.94); knob (4, 0.0f); r.run (1.94);
        knob (3, 0.6f); r.run (3.87); knob (3, 0.0f); r.run (1.0);
        knob (6, 0.7f); r.run (3.87); knob (6, 0.0f);
        knob (2, 0.8f); r.run (3.87);
        knob (7, 0.6f); r.run (3.87);
        r.cc (10, 16, 100);                                        // KILL FX (pad in CC mode)
        r.run (5.0);
        saveWav (r.rec, "pad-demo.wav");
    }
    // RMX check: each sound rolled at 1/4, 1/8, 1/16 at 128 BPM (MIDI clock), like the reference recording
    {
        Rig r; r.track.on = false; r.recording = true; r.clockBpm = 128.0;
        r.run (1.0); r.rec.setSize (2, 0);
        const int soundPads[] = { 40, 41, 42, 43 }, speedPads[] = { 36, 37, 38 };
        for (int sp : soundPads)
        {
            r.note (10, 36, true); r.note (10, 36, false);                 // 1/4 selected (stays lit)
            if (r.p.getActiveSpeed() != 0) { r.note (10, 36, true); r.note (10, 36, false); }
            r.note (10, sp, true); r.note (10, sp, false); r.run (0.3);   // sound on (1/4)
            for (int sd : speedPads)
            {
                if (sd != 36) { r.note (10, sd, true); r.note (10, sd, false); }
                r.run (4.0);
                if (sd == 38) { r.note (10, sp, true); r.note (10, sp, false); }   // sound off
                r.run (0.5);
            }
        }
        saveWav (r.rec, "pad-rmx-check.wav");
    }
    // Revolo noise alone (program 2)
    {
        Rig r; r.track.on = false; r.recording = true;
        r.p.selectProgram (1);
        r.p.knobFromScreen (4, 0.7f, 1);
        r.p.padFromScreen (0, true, 1);
        r.run (8.0);
        r.p.padFromScreen (0, true, 1);   // off: tail
        r.run (4.0);
        saveWav (r.rec, "pad-noise.wav");
    }
    return 0;
}
