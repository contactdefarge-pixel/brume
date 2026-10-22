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
        // PLATE reverb alone: 0.5 s noise burst at several TIME / SIZE settings: decay and level must stay sane
        for (auto set : { std::pair<float, float> { 1.5f, 0.2f }, { 3.0f, 0.5f }, { 8.0f, 1.0f } })
        {
            v10::Plate rv; rv.prepare (fs); rv.setParameters (set.first, set.second, 0.6f);
            for (int i = 0; i < (int) fs; ++i) { float a, b; rv.process (0, 0, a, b); }   // let parameters settle
            juce::Random rnd (7); double inE = 0, outE = 0; std::vector<double> e; double acc = 0; int cnt = 0; float peak = 0;
            for (int i = 0; i < (int) (10.0 * fs); ++i)
            {
                const float x = i < fs * 0.5 ? (rnd.nextFloat() * 2 - 1) * 0.3f : 0.0f; inE += x * x;
                float a, b; rv.process (x, x, a, b); outE += a * a; acc += a * a; peak = std::max (peak, std::abs (a));
                if (++cnt == 512) { e.push_back (10 * std::log10 (acc / 512 + 1e-20)); acc = 0; cnt = 0; }
            }
            const size_t s0 = (size_t) (0.6 * fs / 512); size_t t30 = s0;
            for (size_t i = s0; i < e.size(); ++i) if (e[i] > e[s0] - 30.0) t30 = i;
            std::printf ("plate RT %.1f size %.1f: RT60 measured %.1f s, out/in %5.1f dB, peak %.2f\n", set.first, set.second,
                         2.0 * (double) (t30 - s0) * 512 / fs, 10 * std::log10 (outE / inE), peak);
        }
        // clean delay: a 1 kHz tone through 4 repeats must stay clean (harmonics far below the tone)
        {
            v10::Delay d; d.prepare (fs, 0); d.setTime (0.1 * fs);
            std::vector<float> out;
            for (int i = 0; i < (int) (1.0 * fs); ++i)
            {
                const float x = i < 0.08 * fs ? 0.7f * (float) std::sin (2 * juce::MathConstants<double>::pi * 1000.0 * i / fs) : 0.0f;
                float a, b; d.process (x, x, 0.9f, a, b); out.push_back (a);
            }
            // 4th repeat window
            const int st = (int) (0.42 * fs), len = 2048; double h1 = 0, h3 = 0;
            for (int k = 0; k < len; ++k)
            {
                const double w = 0.5 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * k / len), t = (double) k / fs;
                h1 += out[(size_t) (st + k)] * w * std::sin (2 * juce::MathConstants<double>::pi * 1000.0 * t);
                h3 += out[(size_t) (st + k)] * w * std::sin (2 * juce::MathConstants<double>::pi * 3000.0 * t);
            }
            std::printf ("clean delay          : 3rd harmonic %.1f dB below the tone after 4 repeats\n", 20 * std::log10 (std::abs (h1) / (std::abs (h3) + 1e-12)));
        }
    }
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
        r.cc (10, 74, 76); r.cc (10, 75, 64);   // K5 SIZE/FEEDBACK 60 %, K6 TIME 50 %
        for (int k : { 2, 3, 6, 7 })   // K3 LPF, K4 K7 K8 = the send FX
        {
            r.cc (10, 70 + k, 100); m = r.run (2.0);
            std::printf ("K%d %-15s : %6.1f dB rms, %5.1f dB peak\n", k + 1, knobLabel (0, k), m.rmsDb(), m.peakDb());
            r.cc (10, 70 + k, 0); r.run (1.0);
        }
        // maximum feedback: the repeats must keep going when the send is closed (like the V10), until KILL
        r.cc (10, 74, 127); r.cc (10, 73, 110); r.run (1.0); r.cc (10, 73, 0);   // long delay at max feedback
        r.track.on = false; auto inf = r.run (4.0); r.track.on = true;
        std::printf ("feedback max         : repeats after closing the send %5.1f dB (should still sound)\n", inf.rmsDb());
        r.cc (10, 74, 76);
        r.cc (10, 77, 110); r.cc (10, 73, 100); r.cc (10, 76, 90); r.run (2.0);
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
        r.note (12, 52, true); r.run (0.05); r.cc (12, 90, 5); r.run (0.05);
        r.note (13, 60, true); r.run (0.05); r.cc (13, 100, 5); r.run (0.05);   // program 4 (GLOBAL)
        r.pending.addEvent (juce::MidiMessage::programChange (12, 0), 0); r.run (0.05);   // PROG CHNG + PAD 1
        std::printf ("setup                : step %d, P1 %s, P2 %s, P3 %s, P4 %s\n", r.p.getLearnStep(), r.p.isLearned (0) ? "ok" : "NO", r.p.isLearned (1) ? "ok" : "NO", r.p.isLearned (2) ? "ok" : "NO", r.p.isLearned (3) ? "ok" : "NO");
        r.note (11, 48, true); r.note (11, 48, false); r.run (0.3);   // PAD 5 of program 2 = Brume ON
        std::printf ("program detection    : program %d (expect 1), Brume on %s\n", r.p.getProgram() + 1 - 1, r.p.isBrumeOn() ? "yes" : "NO");
        r.cc (11, 84, 110); m = r.run (3.0);   // K5 = NOISE
        std::printf ("Brume (Revolo noise) : %6.1f dB rms\n", m.rmsDb());
        r.note (10, 36, true); r.note (10, 36, false); r.run (0.05);
        std::printf ("back to program 1    : program %d (expect 0)\n", r.p.getProgram());

        // program 3: Pattern Player. NOTE-mode pad = steps 1-8, PROG CHNG-mode pad = steps 9-16
        r.cc (11, 84, 0); r.note (11, 48, true); r.note (11, 48, false); r.run (0.05);   // Brume off
        const int before = r.p.getPatternSteps();
        r.note (12, 53, true); r.note (12, 53, false); r.run (0.05);   // PAD 2 (bottom row) -> step 6
        const int afterNote = r.p.getPatternSteps();
        r.pending.addEvent (juce::MidiMessage::programChange (12, 4), 0); r.run (0.05);   // PAD 5 in PROG CHNG -> step 9
        const int afterPC = r.p.getPatternSteps();
        std::printf ("P3 steps             : program %d (expect 2), step 6 %s, step 9 %s, page %d (expect 1)\n", r.p.getProgram(),
                     ((afterNote ^ before) == (1 << 5)) ? "toggled" : "NO", ((afterPC ^ afterNote) == (1 << 8)) ? "toggled" : "NO", r.p.getPatternPage());
        r.track.on = false; r.run (1.0);
        m = r.run (2.0);
        std::printf ("P3 volume 0          : %6.1f dB (silent)\n", m.rmsDb());
        r.cc (12, 90, 100); m = r.run (2.0);
        std::printf ("P3 pattern playing   : %6.1f dB rms, %5.1f dB peak\n", m.rmsDb(), m.peakDb());
        // PAD 6 + PAD 7 together = FX target toggle (no step change); program 4: K1 = MASTER, PAD 5 / PAD 6 = FX target
        {
            const int st = r.p.getPatternSteps();
            r.note (12, 57, true); r.note (12, 58, true); r.note (12, 57, false); r.note (12, 58, false); r.run (0.05);
            std::printf ("PAD 6+7 combo        : INPUT %s (expect off), steps unchanged %s\n", r.p.fxInputOn() ? "ON" : "off", r.p.getPatternSteps() == st ? "yes" : "NO");
            r.note (13, 65, true); r.note (13, 65, false); r.cc (13, 100, 64); r.run (0.05);   // P4: PAD 6 = INPUT back on
            std::printf ("V10 FX program       : program %d (expect 3), INPUT %s (expect on), K1 %s\n", r.p.getProgram(), r.p.fxInputOn() ? "on" : "OFF", r.p.getKnobText (3, 0).toRawUTF8());
            r.note (12, 52, true); r.note (12, 52, false); r.run (0.05); r.note (12, 52, true); r.note (12, 52, false); r.run (0.05);
        }
        const float tremBefore = r.p.getKnobValue (1, 5);
        r.p.killFx(); r.run (2.5);
        std::printf ("KILL keeps TREMOLO   : %.2f -> %.2f (expect equal)\n", tremBefore, r.p.getKnobValue (1, 5)); m = r.run (2.0);   // the last hit rings out (DECAY at 100 %)
        std::printf ("KILL (CC) all        : pattern after kill %6.1f dB (expect silent)\n", m.rmsDb());
        r.cc (12, 90, 0); r.track.on = true;

        // switch: in SONS SEULS the music passes untouched whatever the effect sends
        {
            Rig a, b;
            for (auto* x : { &a, &b }) x->p.apvts.getParameter (id::fxInput)->setValueNotifyingHost (0.0f);
            b.cc (10, 77, 120); b.cc (10, 76, 120);   // K8 reverb + K7 dub echo sends wide open on b
            a.run (1.0); b.run (1.0);
            const auto ma = a.run (3.0), mb = b.run (3.0);
            std::printf ("INPUT off            : music %6.1f dB, with FX sends %6.1f dB (expect equal)\n", ma.rmsDb(), mb.rmsDb());
            b.p.apvts.getParameter (id::fxInput)->setValueNotifyingHost (1.0f); b.run (1.0);
            std::printf ("INPUT on             : with FX sends %6.1f dB (expect louder/wetter)\n", b.run (3.0).rmsDb());
        }

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
        r.cc (10, 76, 70); r.cc (10, 74, 40); r.note (10, 38, true); r.note (10, 38, false); r.note (10, 41, true); r.note (10, 41, false); r.run (0.3);
        snap ("pad-p1.png");
        r.note (10, 41, true); r.note (10, 41, false); r.note (11, 45, true); r.note (11, 45, false); r.run (0.3);
        snap ("pad-p2.png");
        r.p.selectProgram (2); r.p.setPatternPage (0); r.cc (12, 90, 90); r.run (0.4);
        snap ("pad-p3.png");
        r.p.setPatternPage (1); r.run (0.1);
        snap ("pad-p3b.png");
        r.p.selectProgram (3); r.p.knobFromScreen (4, 0.5f, 3); r.p.knobFromScreen (7, 0.4f, 3); r.run (0.2);
        snap ("pad-p4.png");
        if (auto* pe = dynamic_cast<PadEditor*> (ed.get())) pe->openUserKit();
        snap ("pad-userkit.png");
    }
    // program 4 (V10 FX): each effect adds a tail after the music stops; VINYL BRAKE stops the sound
    {
        for (int k = 0; k < 4; ++k)
        {
            Rig r; r.p.selectProgram (3);
            r.p.knobFromScreen (4 + k, 0.8f, 3); r.run (3.0);
            r.track.on = false; const auto t = r.run (1.5);
            std::printf ("V10 %-13s     : tail after the music stops %6.1f dB, peak %5.1f dB\n", knobLabel (3, 4 + k), t.rmsDb(), t.peakDb());
        }
        {
            // listening demo: the track, then each effect for 4 beats-ish, music cut to hear the tail, then reverb (P1 K8)
            Rig d; d.recording = true; d.p.selectProgram (3); d.run (2.0);
            for (int k = 0; k < 4; ++k)
            {
                d.track.on = true; d.p.knobFromScreen (4 + k, 0.7f, 3); d.run (3.9);
                d.p.knobFromScreen (k, 0.2f, 3); d.run (1.0); d.p.knobFromScreen (k, 0.8f, 3); d.run (1.0);   // time sweep: pitch glide
                d.p.knobFromScreen (4 + k, 0.0f, 3); d.track.on = false; d.run (2.5);
            }
            d.track.on = true; d.p.selectProgram (0); d.p.knobFromScreen (7, 0.6f, 0); d.p.knobFromScreen (5, 0.6f, 0); d.run (4.0);
            d.p.knobFromScreen (7, 0.0f, 0); d.track.on = false; d.run (4.0);
            saveWav (d.rec, "pad-v10-demo.wav");
        }
        Rig r; r.p.selectProgram (3); r.run (1.0);
        r.p.padFromScreen (4, true, 3); r.run (0.7); const auto stopped = r.run (0.5);
        r.p.padFromScreen (4, false, 3); const auto back = r.run (0.5);
        std::printf ("VINYL BRAKE          : held %6.1f dB (expect silent), released %6.1f dB (music back)\n", stopped.rmsDb(), back.rmsDb());
    }
    // Pattern Player USER kit: a 200 Hz test tone loaded as KICK must be what plays
    {
        juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile ("user-kick-test.wav");
        {
            f.deleteFile(); juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), 44100.0, 1, 16, {}, 0));
            juce::AudioBuffer<float> b (1, 13230);
            for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.5f * (float) std::sin (2 * juce::MathConstants<double>::pi * 200.0 * i / 44100.0));
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        Rig r; r.track.on = false; r.recording = true; r.p.selectProgram (2);
        const bool ok = r.p.loadUserSample (0, f);
        auto set = [&] (const char* pid, float v) { r.p.apvts.getParameter (pid)->setValueNotifyingHost (v); };
        set (id::ppKit, 1.0f); set (id::ppSound, 0.0f); set (id::ppPattern, 0.0f); set (id::ppVolume, 0.8f); set (id::ppDecay, 1.0f);
        const auto um = r.run (2.0);
        // energy at 200 Hz vs 400 Hz over the recording
        double e200 = 0, e400 = 0; const auto* d = r.rec.getReadPointer (0);
        for (int i = 0; i < r.rec.getNumSamples(); ++i)
        {
            e200 += d[i] * std::sin (2 * juce::MathConstants<double>::pi * 200.0 * i / fs);
            e400 += d[i] * std::sin (2 * juce::MathConstants<double>::pi * 400.0 * i / fs);
        }
        std::printf ("USER kit             : loaded %s, kit %s, path kept %s\n", ok ? "yes" : "NO",
                     r.p.apvts.getParameter (id::ppKit)->getCurrentValueAsText().toRawUTF8(), r.p.getUserSamplePath (0).isNotEmpty() ? "yes" : "NO");
        std::printf ("USER kit sound       : %6.1f dB rms, playing the loaded file %s\n", um.rmsDb(), std::abs (e200) > 5 * std::abs (e400) ? "yes" : "NO");
        juce::MemoryBlock mb; r.p.getStateInformation (mb);
        PadProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        std::printf ("USER kit restored    : %s\n", q.getUserSamplePath (0) == f.getFullPathName() ? "yes" : "NO");
        f.deleteFile();
    }
    // tremolo synced to the MIDI clock while K7 TEMPO sweeps across the division boundaries (must not cut)
    {
        Rig r; r.track.on = false; r.recording = true; r.clockBpm = 124.0;
        r.p.selectProgram (1);
        r.p.knobFromScreen (4, 0.7f, 1); r.p.knobFromScreen (5, 0.8f, 1); r.p.knobFromScreen (0, 0.0f, 1);
        r.p.padFromScreen (4, true, 1); r.run (2.0);
        for (int i = 0; i <= 120; ++i) { r.p.knobFromScreen (6, 0.7f * (float) i / 120.0f, 1); r.run (0.1); }
        saveWav (r.rec, "pad-trem-sweep.wav");
    }
    // Pattern Player demo: 909 hats offbeat over the track with duck, then RMX clap backbeat in SONS SEULS
    {
        Rig r; r.recording = true;
        r.p.selectProgram (2);
        auto set = [&] (const char* pid, float v) { r.p.apvts.getParameter (pid)->setValueNotifyingHost (v); };
        set (id::ppKit, 0.5f); set (id::ppSound, 3.0f / 7.0f); set (id::ppPattern, 1.0f / 7.0f); set (id::ppVolume, 0.8f); set (id::ppDecay, 0.3f);
        r.run (4.0);
        set (id::ppSound, 0.0f); set (id::ppPattern, 0.0f); set (id::ppDuck, 0.8f); set (id::ppDecay, 0.7f); r.run (4.0);
        set (id::ppKit, 1.0f); set (id::ppSound, 2.0f / 7.0f); set (id::ppPattern, 2.0f / 7.0f); set (id::ppSwing, 0.5f); set (id::ppDuck, 0.0f);
        set (id::fxInput, 0.0f); set (id::reverb, 0.6f); r.run (6.0);
        saveWav (r.rec, "pad-pattern.wav");
    }

    // demo: track, snare build with speed changes, K1 and K2 up, drop: K1 down + HPF echo throw, reverb, crush,
    // long delay, Revolo noise, KILL FX (tails ring out)
    {
        Rig r; r.recording = true;
        auto knob = [&] (int slot, float v) { static const int phys[] = { 0, 1, 2, 3, 6, 7, 4, 5 }; r.cc (10, 70 + phys[slot], juce::roundToInt (v * 127.0f)); };
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
        knob (6, 0.6f); knob (7, 0.6f);
        knob (4, 0.7f); r.run (1.94); knob (4, 0.0f); r.run (1.94);     // dub echo throw
        knob (5, 0.6f); r.run (3.87); knob (5, 0.0f); r.run (1.0);      // reverb
        knob (2, 0.6f); r.run (3.87); knob (2, 0.0f);                   // short delay
        knob (3, 0.7f); r.run (3.87);                                   // long delay
        knob (6, 0.95f); r.run (3.87);                                  // feedback up
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
        r.p.padFromScreen (4, true, 1);
        r.run (8.0);
        r.p.padFromScreen (4, true, 1);   // off: tail
        r.run (4.0);
        saveWav (r.rec, "pad-noise.wav");
    }
    return 0;
}
