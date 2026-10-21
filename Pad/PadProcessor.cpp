#include "PadProcessor.h"
#include "PadEditor.h"
#if JUCE_MAC
 #include <mach-o/dyld.h>
 #include <filesystem>
 #include <cstdlib>
 #include <cstring>
#endif

using namespace bpad;

#if JUCE_MAC
namespace
{
    // "Brume Pad" became "Hydra". Runs before the standalone app starts (so the audio settings are found):
    // keeps the old settings (audio devices, LPD8 setup), removes the old plugin bundles (same plugin ID),
    // and renames an app that an old updater installed as "Brume Pad.app".
    struct HydraMigration
    {
        HydraMigration()
        {
            if (std::strcmp (getprogname(), "Hydra") != 0) return;
            namespace fs = std::filesystem;
            std::error_code ec;
            const char* home = std::getenv ("HOME");
            if (home == nullptr) return;
            const fs::path h (home);
            const auto as = h / "Library" / "Application Support";
            if (! fs::exists (as / "Hydra.settings", ec) && fs::exists (as / "Brume Pad.settings", ec))
                fs::copy_file (as / "Brume Pad.settings", as / "Hydra.settings", ec);
            const auto pl = h / "Library" / "Audio" / "Plug-Ins";
            fs::remove_all (pl / "VST3" / "Brume Pad.vst3", ec);
            fs::remove_all (pl / "Components" / "Brume Pad.component", ec);
            char buf[4096]; uint32_t size = sizeof (buf);
            if (_NSGetExecutablePath (buf, &size) == 0)
            {
                const auto bundle = fs::weakly_canonical (fs::path (buf), ec).parent_path().parent_path().parent_path();
                if (bundle.filename() == "Brume Pad.app")
                {
                    const auto dst = bundle.parent_path() / "Hydra.app";
                    if (! fs::exists (dst, ec)) fs::rename (bundle, dst, ec);
                }
            }
        }
    } hydraMigration;
}
#endif

namespace
{
    constexpr int kScreenChannel = 16;   // events from the editor travel as MIDI on channel 16 (prog * 8 + pad / knob)
    double positiveMod (double x, double m) { const double r = std::fmod (x, m); return r < 0 ? r + m : r; }
    juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
}

//==============================================================================
PadProcessor::PadProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PAD", createLayout())
{
    brumeEngine.setRevoloNoise (true);
    brumeEngine.setSlowNoiseDown (true);
    brumeEngine.setFreeTremolo (true);
    brumeEngine.setNoiseTrim (0.4f);    // white noise -8 dB here (it was far too loud next to the music)   // K7 TEMPO = a plain rate in Hz, glides without any cut
    brumeEngine.setPlayHead (&brumeHead);
    for (auto& d : padDown) d.store (false);
    for (auto& h : padHit) h.store (-10.0);
    for (auto& k : fxKilledUi) k.store (false);
    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        startTimerHz (1);
        startLink();
    }
}

void PadProcessor::startLink()
{
   #if BRUME_HAS_LINK
    // Ableton Link: Traktor's LINK button shares tempo and beat over the network (also on the same Mac)
    if (link != nullptr) return;
    link = std::make_unique<ableton::Link> (124.0);
    link->setNumPeersCallback ([this] (std::size_t n) { linkPeers.store ((int) n); });
    link->enable (true);
   #endif
}

PadProcessor::~PadProcessor()
{
    stopTimer();
   #if BRUME_HAS_LINK
    if (link) link->enable (false);
   #endif
    for (auto* in : openInputs) in->stop();
}

juce::AudioProcessorValueTreeState::ParameterLayout PadProcessor::createLayout()
{
    using F = juce::AudioParameterFloat; using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
    const juce::NormalisableRange<float> r (0.0f, 1.0f);
    auto add = [&] (const char* pid, const char* name, float def, std::function<juce::String (float, int)> txt)
    { ps.push_back (std::make_unique<F> (juce::ParameterID { pid, 1 }, name, r, def, A().withStringFromValueFunction (txt))); };
    auto fxText = [] (float v, int) { return v < 0.01f ? juce::String ("OFF") : juce::String (juce::roundToInt (v * 100.0f)) + " %"; };

    add (id::drums, "Drums Volume", 0.8f, pct);
    add (id::pitch, "Drums Pitch", 0.5f, [] (float v, int) { const int s = juce::roundToInt (-12.0f + 24.0f * v); return (s > 0 ? "+" : "") + juce::String (s) + " st"; });
    add (id::shortDelay, "Short Delay Send", 0.0f, fxText);
    add (id::lpf, "LPF", 0.0f, [] (float v, int) { return v < 0.01f ? juce::String ("OFF") : juce::String (juce::roundToInt (20000.0f * std::pow (150.0f / 20000.0f, v))) + " Hz"; });
    add (id::longDelay, "Long Delay Send", 0.0f, fxText);
    add (id::dubEcho, "Dub Echo Send", 0.0f, fxText);
    add (id::reverb, "Reverb Send", 0.0f, fxText);
    add (id::size, "Size / Feedback", 0.5f, pct);
    add (id::time, "Time", 0.5f, pct);
    add (id::sound, "Drum Sound", 1.0f / 3.0f, [] (float v, int) { return juce::String (soundName ((int) std::lround (v * 3.0f))); });
    ps.push_back (std::make_unique<F> (juce::ParameterID { id::bpm, 1 }, "Tempo (no sync)", juce::NormalisableRange<float> (60.0f, 200.0f, 0.1f), 124.0f,
                                       A().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " BPM"; })));
    add (id::master, "Master", 0.8f, pct);
    ps.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::fxInput, 1 }, "FX Source Input", true));
    ps.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::fxHydra, 1 }, "FX Source Hydra", true));
    add (id::ppVolume, "Pattern Volume", 0.0f, fxText);
    add (id::ppPitch, "Pattern Pitch", 0.5f, [] (float v, int) { const int s = juce::roundToInt (-12.0f + 24.0f * v); return (s > 0 ? "+" : "") + juce::String (s) + " st"; });
    add (id::ppDecay, "Pattern Decay", 1.0f, pct);
    add (id::ppDuck, "Pattern Duck", 0.0f, fxText);
    add (id::ppKit, "Pattern Kit", 0.0f, [] (float v, int) { return juce::String (pad::pp::kitName (stepsOf (v, pad::pp::numKits))); });
    add (id::ppSound, "Pattern Sound", 0.0f, [] (float v, int) { return juce::String (pad::pp::soundName (stepsOf (v, pad::pp::soundsPerKit))); });
    add (id::ppPattern, "Pattern", 0.0f, [] (float v, int) { return "PATTERN " + juce::String (stepsOf (v, pad::pp::numPatterns) + 1); });
    add (id::ppSwing, "Pattern Swing", 0.0f, pct);
    ps.push_back (std::make_unique<F> (juce::ParameterID { id::syncOffset, 1 }, "Sync Offset", juce::NormalisableRange<float> (-60.0f, 60.0f, 1.0f), 0.0f,
                                       A().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " ms"; })));
    return { ps.begin(), ps.end() };
}

bool PadProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && (layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() || layouts.getMainInputChannelSet().isDisabled());
}

juce::AudioProcessorEditor* PadProcessor::createEditor() { return new PadEditor (*this); }

//==============================================================================
void PadProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxBlock = std::max (32, samplesPerBlock);
    brumeEngine.setRateAndBufferSizeDetails (fs, maxBlock);
    brumeEngine.prepareToPlay (fs, maxBlock);
    kit.prepare (fs);
    shortFx.prepare (fs, 3); longFx.prepare (fs, 3); dubFx.prepare (fs, 4);
    shortFx.line.glide = true; longFx.line.glide = true;   // TIME bends the repeats like tape (DJM-V10)
    lpfL.reset(); lpfR.reset(); lpfCut = -1.0f;
    hydBuf.setSize (1, maxBlock);
    reverbFx.prepare (fs);
    revBuf.setSize (2, maxBlock);
    musicBuf.setSize (2, maxBlock);
    ppKits.prepare (fs, kit.s);
    queue.reset (fs);
    queueReady.store (true);
}

//==============================================================================
// MIDI inputs (standalone): the LPD8 and Traktor's clock port are opened automatically
void PadProcessor::timerCallback()
{
    bool lpd8 = false, traktor = false;
    for (const auto& dev : juce::MidiInput::getAvailableDevices())
    {
        const bool isLpd8 = dev.name.containsIgnoreCase ("LPD8");
        const bool isTraktor = dev.name.containsIgnoreCase ("Traktor Virtual Output");
        lpd8 |= isLpd8; traktor |= isTraktor;
        if ((isLpd8 || isTraktor) && ! openNames.contains (dev.identifier))
            if (auto in = juce::MidiInput::openDevice (dev.identifier, this))
            {
                in->start();
                openInputs.add (in.release());
                openNames.add (dev.identifier);
            }
    }
    lpd8Seen.store (lpd8); traktorSeen.store (traktor);
}

void PadProcessor::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (! queueReady.load() || m.isActiveSense()) return;
    auto msg = m;
    msg.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    queue.addMessageToQueue (msg);
}

void PadProcessor::padFromScreen (int pad, bool down, int prog)
{
    if (! queueReady.load()) return;
    auto m = down ? juce::MidiMessage::noteOn (kScreenChannel, prog * 8 + pad, (juce::uint8) 110) : juce::MidiMessage::noteOff (kScreenChannel, prog * 8 + pad);
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    queue.addMessageToQueue (m);
}

void PadProcessor::knobFromScreen (int knob, float v, int prog)
{
    if (! queueReady.load()) return;
    auto m = juce::MidiMessage::controllerEvent (kScreenChannel, prog * 8 + knob, juce::jlimit (0, 127, juce::roundToInt (v * 127.0f)));
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    queue.addMessageToQueue (m);
}

void PadProcessor::setParam (juce::AudioProcessorValueTreeState& t, const char* pid, float v)
{
    if (auto* prm = t.getParameter (pid))
        if (std::abs (prm->getValue() - v) > 1.0e-4f)
            prm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
}

void PadProcessor::handleMidi (const juce::MidiMessage& m, juce::int64 samplePos)
{
    // ---- clock (Traktor) ----
    if (m.isMidiClock())
    {
        if (lastClockSample >= 0)
        {
            const double d = (double) (samplePos - lastClockSample);
            if (d > 0.0 && d < fs * 0.25)
                clockTickSamples = clockTickSamples <= 0.0 ? d : clockTickSamples * 0.92 + d * 0.08;
        }
        lastClockSample = samplePos;
        clockTicks += 1.0;
        clockForBrume.addEvent (m, (int) juce::jlimit ((juce::int64) 0, (juce::int64) maxBlock - 1, samplePos - sampleCounter));
        return;
    }
    if (m.isMidiStart() || m.isSongPositionPointer())
    {
        if (m.isMidiStart()) clockTicks = -1.0;
        else clockTicks = m.getSongPositionPointerMidiBeat() * 6.0 - 1.0;
        clockForBrume.addEvent (m, 0);
        return;
    }
    const int ch = m.getChannel();
    if (m.isProgramChange())
    {
        if (learnStep.load() == 9) { pcBase.store (m.getProgramChangeNumber()); program.store (2); learnStep.store (0); setEvent ("SETUP OK"); return; }
        // pads in PROG CHNG mode: steps 9-16 of the Pattern Player (program 3)
        const auto& m3 = maps[2];
        if (m3.learned && ch == m3.padChannel) program.store (2);
        if (program.load() == 2)
        {
            ppPage.store (1);
            const int pad = ((m.getProgramChangeNumber() - pcBase.load()) % 8 + 8) % 8;
            const int step = 8 + stepOfPad (pad);
            ppSteps.store (ppSteps.load() ^ (1 << step));
            padHit[(size_t) pad].store (uiTime());
            setEvent ("STEP " + juce::String (step + 1));
        }
        return;
    }
    if (! (m.isNoteOnOrOff() || m.isController())) return;
    // ---- clicks on the screen ----
    if (ch == kScreenChannel)
    {
        if (m.isController())  handleKnob (m.getControllerNumber() / 8, m.getControllerNumber() % 8, (float) m.getControllerValue() / 127.0f);
        else                   handlePad (m.getNoteNumber() / 8, m.getNoteNumber() % 8, m.isNoteOn(), m.getFloatVelocity());
        return;
    }

    // ---- LPD8 setup: learn where each program sends its pads and knobs ----
    const int step = learnStep.load();
    if (step == 9) return;   // waiting for a Program Change (PROG CHNG + PAD 1)
    if (step > 0)
    {
        auto& mp = maps[(size_t) ((step - 1) / 2)];
        if ((step % 2 == 1) && m.isNoteOn()) { mp.padChannel = ch; mp.padNote0 = m.getNoteNumber(); learnStep.store (step + 1); return; }
        if ((step % 2 == 0) && m.isController())
        {
            mp.knobChannel = ch; mp.knobCC0 = m.getControllerNumber(); mp.learned = true;
            learnStep.store (step + 1);   // after 8: PROG CHNG pad 1 of program 3
            return;
        }
        return;
    }

    if (m.isNoteOnOrOff())
    {
        const int note = m.getNoteNumber();
        for (int p = 0; p < numProgs; ++p)
        {
            const auto& mp = maps[(size_t) p];
            if (mp.learned && ch == mp.padChannel && note >= mp.padNote0 && note < mp.padNote0 + numPads)
            {
                program.store (p);
                if (p == 2) ppPage.store (0);   // NOTE mode = steps 1-8
                handlePad (p, note - mp.padNote0, m.isNoteOn(), m.getFloatVelocity());
                return;
            }
        }
        if (note >= 36 && note < 44)   // not set up yet
        {
            if (program.load() == 2) ppPage.store (0);
            handlePad (program.load(), note - 36, m.isNoteOn(), m.getFloatVelocity());
            return;
        }
        return;
    }

    const int cc = m.getControllerNumber();
    const float v = (float) m.getControllerValue() / 127.0f;
    for (int p = 0; p < numProgs; ++p)
    {
        const auto& mp = maps[(size_t) p];
        if (mp.learned && ch == mp.knobChannel && cc >= mp.knobCC0 && cc < mp.knobCC0 + numKnobs)
        { program.store (p); handleKnob (p, cc - mp.knobCC0, v); return; }
    }
    if (cc >= 70 && cc < 78) { handleKnob (program.load(), cc - 70, v); return; }
    if (cc >= 1 && cc < 9)   { handleKnob (program.load(), cc - 1, v); return; }
    // any other CC = a pad in CC mode (CC button on the LPD8): KILL FX
    if (m.getControllerValue() > 0) doKill();
}

void PadProcessor::handlePad (int prog, int pad, bool down, float velocity)
{
    padDown[(size_t) pad].store (down);
    if (down) padHit[(size_t) pad].store (uiTime());

    // two pads together (any program): PAD 5 + PAD 8 = KILL FX, PAD 6 + PAD 7 = FX on music + sounds / sounds only
    if (down && pad >= 4)
    {
        const double now = (double) sampleCounter / fs;
        if (now - lastComboPress > 0.15)   // first pad of a possible combo: remember what it is about to change
        {
            soundBeforeCombo = apvts.getRawParameterValue (id::sound)->load(); activeBeforeCombo = activeSound.load();
            stepsBeforeCombo = ppSteps.load();
        }
        lastComboPress = now;
        comboTime[(size_t) pad] = now;
        auto pair = [&] (int a, int b) { return (pad == a || pad == b) && std::abs (comboTime[(size_t) a] - comboTime[(size_t) b]) < 0.15; };
        auto restore = [&]   // the first pad of the combo is not a choice
        {
            if (prog == 0) { setParam (apvts, id::sound, soundBeforeCombo); activeSound.store (activeBeforeCombo); }
            if (prog == 2) ppSteps.store (stepsBeforeCombo);
        };
        if (pair (4, 7)) { restore(); doKill(); comboTime.fill (-10.0); return; }
        if (pair (5, 6)) { restore(); toggleSource (id::fxInput); comboTime.fill (-10.0); return; }
    }

    if (prog == 3)   // GLOBAL page
    {
        if (! down) return;
        if (pad == 4) toggleSource (id::fxInput);
        if (pad == 5) toggleSource (id::fxHydra);
        return;
    }

    if (prog == 0)
    {
        // one press = on, one press = off (no holding): sound pads start / stop the roll, speed pads pick the speed
        juce::ignoreUnused (velocity);
        if (! down) return;
        auto restartGrid = [this] (int speed)
        {
            const float rate = rollPerBeat (speed);
            const double pos = beat * rate;
            lastRollIdx = (juce::int64) std::floor (pos) - (pos - std::floor (pos) < 0.3 ? 1 : 0);   // a press just after the grid plays at once
            lastRollRate = rate;
        };
        if (pad >= 4)
        {
            const int snd = pad - 4;
            if (activeSound.load() == snd) { activeSound.store (-1); setEvent (juce::String (soundName (snd)) + " OFF"); return; }
            const bool wasPlaying = activeSound.load() >= 0;
            activeSound.store (snd);
            setParam (apvts, id::sound, (float) snd / 3.0f);
            if (activeSpeed.load() < 0) activeSpeed.store (0);
            if (! wasPlaying) restartGrid (activeSpeed.load());
            setEvent (juce::String (soundName (snd)) + " ON  " + juce::String (padLabel (0, activeSpeed.load())));
            return;
        }
        if (activeSpeed.load() == pad) { activeSpeed.store (-1); setEvent (juce::String (padLabel (0, pad)) + " OFF"); return; }
        activeSpeed.store (pad);
        restartGrid (pad);
        setEvent (juce::String ("SPEED ") + padLabel (0, pad));
        return;
    }

    if (prog == 2)
    {
        if (! down) return;
        const int step = ppPage.load() * 8 + stepOfPad (pad);   // the page shown (screen) or set by NOTE / PROG CHNG
        ppSteps.store (ppSteps.load() ^ (1 << step));
        setEvent ("STEP " + juce::String (step + 1) + ((ppSteps.load() >> step) & 1 ? " ON" : " OFF"));
        return;
    }

    if (prog == 1)
    {
        auto& b = brumeEngine.apvts;
        switch (pad)   // top row only (pads 5-8); the bottom row does nothing here
        {
            case 4: if (down) setParam (b, brume::id::on, b.getRawParameterValue (brume::id::on)->load() > 0.5f ? 0.0f : 1.0f); break;
            case 5: if (down) brumeEngine.tap(); break;
            case 7: setParam (b, brume::id::on, down ? 1.0f : 0.0f); break;   // HOLD: on while the pad is held
            default: break;
        }
    }
}

void PadProcessor::handleKnob (int prog, int knob, float v)
{
    if (prog == 0) setParam (apvts, p1KnobId (p1Slot (knob)), v);
    else if (prog == 1) setParam (brumeEngine.apvts, p2KnobId (knob), v);
    else if (prog == 2) setParam (apvts, p3KnobId (knob), v);
    else if (prog == 3 && knob == 0) setParam (apvts, id::master, v);
    else if (prog == 3 && knob == 1) setParam (apvts, id::shortDelay, v);
}

void PadProcessor::toggleSource (const char* pid)
{
    const bool on = apvts.getRawParameterValue (pid)->load() < 0.5f;
    setParam (apvts, pid, on ? 1.0f : 0.0f);
    setEvent (juce::String ("FX SOURCE ") + (pid == id::fxInput ? "INPUT" : "HYDRA") + (on ? " ON" : " OFF"));
}

void PadProcessor::doKill()
{
    for (int k : { 2, 3, 4, 5, 8 }) fx[(size_t) k].kill();   // LPF opens, sends close (tails ring out)
    setParam (brumeEngine.apvts, brume::id::on, 0.0f);   // Brume stops, its tail fades with LENGTH
    activeSound.store (-1);                              // X-PAD: sound and speed pads all off
    activeSpeed.store (-1);
    ppSteps.store (0);                                   // Pattern Player: every step off (press steps or turn K7 to start again)
    killTime.store (uiTime());
    setEvent ("KILL FX");
}

void PadProcessor::triggerSound (int sound, float vel, float gateSeconds)
{
    const float semis = -12.0f + 24.0f * apvts.getRawParameterValue (id::pitch)->load();
    voices[(size_t) nextVoice].trigger (kit.s[(size_t) sound], sound, (float) fs, semis, vel, gateSeconds, 0.5f);
    nextVoice = (nextVoice + 1) % (int) voices.size();
}

//==============================================================================
void PadProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (getTotalNumInputChannels() == 0) buffer.clear();
    if (revBuf.getNumSamples() < n) revBuf.setSize (2, n, false, false, true);
    revBuf.clear();

    const juce::int64 blockStart = sampleCounter;
    clockForBrume.clear();
    {
        juce::MidiBuffer in;
        if (! juce::JUCEApplicationBase::isStandaloneApp())
            for (const auto meta : midi) in.addEvent (meta.getMessage(), meta.samplePosition);
        juce::MidiBuffer q;
        queue.removeNextBlockOfMessages (q, n);
        for (const auto meta : q) in.addEvent (meta.getMessage(), meta.samplePosition);
        for (const auto meta : in) handleMidi (meta.getMessage(), blockStart + meta.samplePosition);
    }
    midi.clear();
    if (killRequest.exchange (false)) doKill();

    // ---- tempo: Traktor's MIDI clock, else the host, else internal ----
    int src = 0;
    double bpm = apvts.getRawParameterValue (id::bpm)->load(), target = 0.0;
    if (clockTickSamples > 0.0 && lastClockSample >= 0 && blockStart - lastClockSample < (juce::int64) (0.5 * fs))
    {
        src = 1;
        bpm = fs * 60.0 / (clockTickSamples * 24.0);
        target = (clockTicks + (double) (blockStart - lastClockSample) / clockTickSamples) / 24.0
               + apvts.getRawParameterValue (id::syncOffset)->load() * 0.001 * bpm / 60.0;
    }
   #if BRUME_HAS_LINK
    else if (link != nullptr && linkPeers.load() > 0)
    {
        // Traktor's music reaches us through BlackHole one input buffer late, and leaves with our drums through
        // the same output: so the drums follow the beat of the music arriving now (now - input buffer), plus the
        // user's fine tuning (SYNC OFFSET, + = drums later)
        const double offsetMs = apvts.getRawParameterValue (id::syncOffset)->load();
        const auto t = link->clock().micros() - std::chrono::microseconds ((long long) (1.0e6 * n / fs + 2000.0 - offsetMs * 1000.0));
        const auto session = link->captureAudioSessionState();
        src = 3; bpm = session.tempo(); target = session.beatAtTime (t, 4.0);
    }
   #endif
    else if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
            if (pos->getIsPlaying() && pos->getBpm() && pos->getPpqPosition())
            { src = 2; bpm = *pos->getBpm(); target = *pos->getPpqPosition(); }
    }
    bpm = juce::jlimit (40.0, 250.0, bpm);
    if (src != 0)
    {
        const double diff = target - beat;
        if (std::abs (diff) > 0.5 || syncSource.load() != src) { beat = target; lastRollIdx = (juce::int64) std::floor (beat * std::max (1.0f, lastRollRate)); }
        else beat += diff * 0.2;
    }
    syncSource.store (src);
    bpmUi.store ((float) bpm);
    const double inc = bpm / 60.0 / fs;
    const double beatAtStart = beat;

    // ---- effect knobs follow their parameters (screen, controller, host automation); a KILL holds until the knob moves ----
    for (int k = 2; k < (int) fx.size(); ++k)
    {
        const float pv = apvts.getRawParameterValue (p1KnobId (k))->load();
        if (std::abs (pv - fx[(size_t) k].target) > 1.0e-4f) fx[(size_t) k].set (pv);
        fxKilledUi[(size_t) k].store (fx[(size_t) k].killed && fx[(size_t) k].target > 0.01f);
    }

    // ---- our sounds go on their own bus; the music is kept aside (MUSIQUE / SONS SEULS switch) ----
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    if (musicBuf.getNumSamples() < n) musicBuf.setSize (2, n, false, false, true);
    for (int c = 0; c < 2; ++c) musicBuf.copyFrom (c, 0, buffer, c, 0, n);
    buffer.clear (0, 0, n); buffer.clear (1, 0, n);
    const bool srcInput = fxInputOn(), srcHydra = fxHydraOn();
    if (hydBuf.getNumSamples() < n) hydBuf.setSize (1, n, false, false, true);
    auto* hy = hydBuf.getWritePointer (0);
    auto* mL = musicBuf.getWritePointer (0);
    auto* mR = musicBuf.getWritePointer (1);

    // Pattern Player settings
    const int patIdx = stepsOf (apvts.getRawParameterValue (id::ppPattern)->load(), pad::pp::numPatterns);
    if (patIdx != lastPattern) { if (lastPattern >= 0) ppSteps.store (pad::pp::patternMask (patIdx)); lastPattern = patIdx; }
    const float ppVol = apvts.getRawParameterValue (id::ppVolume)->load();
    const float ppGain = ppVol * ppVol * 1.6f;
    const float ppSemis = -12.0f + 24.0f * apvts.getRawParameterValue (id::ppPitch)->load();
    const float ppDecayV = apvts.getRawParameterValue (id::ppDecay)->load();
    const float ppGate = 0.03f + 2.0f * ppDecayV * ppDecayV;
    const float duckAmt = srcInput ? apvts.getRawParameterValue (id::ppDuck)->load() : 0.0f;
    const float duckCoef = std::exp (-1.0f / ((0.05f + 0.35f * ppDecayV) * (float) fs));
    const int ppKitI = stepsOf (apvts.getRawParameterValue (id::ppKit)->load(), pad::pp::numKits);
    const int ppSoundI = stepsOf (apvts.getRawParameterValue (id::ppSound)->load(), pad::pp::soundsPerKit);
    const float ppSwingV = apvts.getRawParameterValue (id::ppSwing)->load();

    // ---- 1) drums (X-PAD rolls) and Pattern Player ----
    {
        const float dv = apvts.getRawParameterValue (id::drums)->load();
        const float drumGain = dv * dv * 1.75f;
        const int sound = std::max (0, activeSound.load());
        double b = beat;
        for (int i = 0; i < n; ++i)
        {
            const int held = activeSound.load() >= 0 ? activeSpeed.load() : -1;
            if (held >= 0)
            {
                const float rate = rollPerBeat (held);
                if (rate != lastRollRate) { lastRollIdx = (juce::int64) std::floor (b * rate); lastRollRate = rate; }
                const auto ri = (juce::int64) std::floor (b * rate);
                if (ri > lastRollIdx)
                {
                    // the RMX cuts each hit to the length of the division (a 1/16 kick is short)
                    triggerSound (sound, 1.0f, (float) (60.0 / bpm / rate) - 0.012f);
                    lastRollIdx = ri;
                }
            }
            // Pattern Player: 16 steps locked to the bar (so it always restarts on beat 1), odd steps late with swing
            {
                const double sp = b * 4.0;
                auto idx = (juce::int64) std::floor (sp);
                if ((idx & 1) && sp - (double) idx < ppSwingV * 0.5) --idx;
                if (idx > lastPPStep || idx < lastPPStep - 4)
                {
                    lastPPStep = idx;
                    const int step = (int) (((idx % 16) + 16) % 16);
                    ppPlayStep.store (step);
                    if (ppVol > 0.01f && ((ppSteps.load() >> step) & 1))
                    {
                        ppVoices[(size_t) nextPP].trigger (ppKits.s[(size_t) ppKitI][(size_t) ppSoundI], ppSoundI, (float) fs, ppSemis, 1.0f, ppGate, 0.5f);
                        nextPP = (nextPP + 1) % (int) ppVoices.size();
                        duckEnv = 1.0f;
                    }
                }
            }
            float y = 0.0f;
            for (auto& v : voices) if (v.active) y += v.tick();
            y *= drumGain;
            float py = 0.0f;
            for (auto& v : ppVoices) if (v.active) py += v.tick();
            y += py * ppGain;
            // DUCK: the music dips under each pattern hit (only when the effects act on the music)
            const float g = 1.0f - duckAmt * 0.85f * duckEnv;
            duckEnv *= duckCoef;
            // FX SOURCE: each source goes into the effects, or is kept aside and added dry at the end
            const float ml = srcInput ? mL[i] * g : 0.0f, mr = srcInput ? mR[i] * g : 0.0f;
            if (srcInput) { mL[i] = 0.0f; mR[i] = 0.0f; }
            hy[i] = srcHydra ? 0.0f : y;
            L[i] = ml + (srcHydra ? y : 0.0f);
            R[i] = mr + (srcHydra ? y : 0.0f);
            b += inc;
        }
        beat = b;
    }

    // ---- 2) program 2: Brume (noise voiced like the Revolo, reverb, tremolo), synced on the same clock ----
    brumeHead.bpm.store (bpm); brumeHead.ppq.store (beatAtStart); brumeHead.playing.store (src == 3);   // Link -> Brume's tremolo
    brumeEngine.processBlock (buffer, clockForBrume);

    // ---- 3) program 1: DJM-V10 style send effects (SHORT DELAY, LONG DELAY, DUB ECHO, REVERB) ----
    // K3-K6 = send to each effect (returns are fixed, so tails ring when a send is closed or KILLed),
    // K7 = SIZE / FEEDBACK (feedback of the delays and echo, size of the reverb), K8 = TIME
    const double spb = 60.0 / bpm * fs;   // samples per beat
    const float size = fx[6].target, timeK = fx[7].target;
    {
        static const double dubBeats[]   = { 0.125, 0.1875, 0.25, 0.375, 0.5, 0.75, 1.0 };
        auto pick = [timeK] (const double* t, int count) { return t[juce::jlimit (0, count - 1, (int) (timeK * (float) count))]; };
        // short and long delays: TIME is continuous and the repeats bend in pitch while it moves (tape style)
        shortFx.line.setDelay (spb / 32.0 * std::pow (4.0, (double) timeK));   // 1/32 .. 1/8 beat
        longFx.line.setDelay (spb / 4.0 * std::pow (4.0, (double) timeK));     // 1/4 .. 1 beat
        dubFx.line.setDelay (pick (dubBeats, 7) * spb);
        shortFx.setFilter (12000.0f);
        longFx.setFilter (8000.0f);
        dubFx.setFilter (2600.0f, 0.8f);
        // time = small and short -> long and present; size = small room -> huge empty room with more low end
        reverbFx.setParameters (0.6f * std::pow (15.0f, timeK), size, 0.75f - 0.3f * size);
    }
    // feedback: low (repeats die fast) -> maximum (repeats go on until the effect is cut). A KILL caps it so tails end.
    auto feedback = [&] (int k) { const float f = 0.1f + 0.88f * size; return fx[(size_t) k].killed ? std::min (f, 0.5f) : f; };
    const float fbShort = feedback (8), fbLong = feedback (3), fbDub = std::min (0.97f, feedback (4) * 1.02f);

    const float smoothFast = 1.0f - std::exp (-1.0f / (0.03f * (float) fs));
    auto* rl = revBuf.getWritePointer (0); auto* rr = revBuf.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        for (int k : { 2, 3, 4, 5, 8 })
            sm[k] += (fx[(size_t) k].effective() - sm[k]) * smoothFast;

        // LPF (K3): open at 0, down to 150 Hz at max, a touch of resonance
        if ((i & 15) == 0 && std::abs (sm[2] - lpfCut) > 1.0e-4f)
        {
            lpfCut = sm[2];
            const float hz = 20000.0f * std::pow (150.0f / 20000.0f, lpfCut);
            lpfL.set ((float) fs, hz, 0.9f + 0.6f * lpfCut); lpfR.set ((float) fs, hz, 0.9f + 0.6f * lpfCut);
        }
        float l = L[i], r = R[i];
        {
            const float fl = lpfL.tick (l).lp, fr = lpfR.tick (r).lp, mix = std::min (1.0f, sm[2] * 12.0f);
            l += (fl - l) * mix; r += (fr - r) * mix;
        }
        float el = 0.0f, er = 0.0f, o0, o1;
        shortFx.process (l * sm[8], r * sm[8], fbShort, 0.0f, o0, o1);  el += o0 * 0.8f;  er += o1 * 0.8f;
        longFx.process (l * sm[3], r * sm[3], fbLong, 0.0f, o0, o1);    el += o0 * 0.8f;  er += o1 * 0.8f;
        dubFx.process (l * sm[4], r * sm[4], fbDub, 0.0f, o0, o1);      el += o0 * 0.95f; er += o1 * 0.95f;

        rl[i] = (l * sm[5] + el * 0.25f * sm[5]) * 0.8f;
        rr[i] = (r * sm[5] + er * 0.25f * sm[5]) * 0.8f;
        L[i] = l + el;
        R[i] = r + er;
    }
    reverbFx.process (rl, rr, n);
    const float master = std::pow (apvts.getRawParameterValue (id::master)->load(), 2.0f) * 1.25f;
    // soft clip that leaves everything below -1 dB untouched (dry music passes cleanly)
    auto clip = [] (float x) { const float a = std::abs (x); if (a <= 0.89f) return x; const float y = 0.89f + 0.11f * std::tanh ((a - 0.89f) / 0.11f); return x < 0.0f ? -y : y; };
    for (int i = 0; i < n; ++i)
    {
        L[i] = clip ((L[i] + rl[i] * 1.4f + hy[i]) * master + mL[i]);   // mL / hy hold only the sources kept dry
        R[i] = clip ((R[i] + rr[i] * 1.4f + hy[i]) * master + mR[i]);
    }
    for (int c = 0; c < 2; ++c)
    {
        const float pk = buffer.getMagnitude (c, 0, n);
        auto& a = peakOut[(size_t) c];
        if (pk > a.load()) a.store (pk);
    }

    sampleCounter += n;
    barBeatUi.store ((float) positiveMod (beatAtStart, 4.0));
}

//==============================================================================
float PadProcessor::padFlash (int p) const
{
    const double dt = uiTime() - padHit[(size_t) p].load();
    return dt < 0.0 || dt > 0.2 ? 0.0f : (float) (1.0 - dt / 0.2);
}

float PadProcessor::getKillFlash() const
{
    const double dt = uiTime() - killTime.load();
    return dt < 0.0 || dt > 0.6 ? 0.0f : (float) (1.0 - dt / 0.6);
}

float PadProcessor::getKnobValue (int prog, int k) const
{
    if (prog == 0) return apvts.getRawParameterValue (p1KnobId (p1Slot (k)))->load();
    if (prog == 3) return k == 0 ? apvts.getRawParameterValue (id::master)->load() : k == 1 ? apvts.getRawParameterValue (id::shortDelay)->load() : 0.0f;
    if (prog == 1) if (auto* p = brumeEngine.apvts.getParameter (p2KnobId (k))) return p->getValue();
    if (prog == 2) return apvts.getRawParameterValue (p3KnobId (k))->load();
    return 0.0f;
}

juce::String PadProcessor::getKnobText (int prog, int k) const
{
    if (prog == 0) if (auto* p = apvts.getParameter (p1KnobId (p1Slot (k)))) return p->getCurrentValueAsText();
    if (prog == 3)
    {
        if (k == 1) return apvts.getParameter (id::shortDelay)->getCurrentValueAsText();
        if (k != 0) return {};
        const float db = masterDb (apvts.getRawParameterValue (id::master)->load());
        return db <= -99.0f ? juce::String ("-INF") : (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
    }
    if (prog == 1 && k == 6) { const float hz = brume::tremoloRateHz (brumeEngine.apvts.getRawParameterValue (brume::id::tempo)->load()); return juce::String (hz, hz < 10.0f ? 2 : 1) + " Hz"; }
    if (prog == 1) if (auto* p = brumeEngine.apvts.getParameter (p2KnobId (k))) return p->getCurrentValueAsText();
    if (prog == 2) if (auto* p = apvts.getParameter (p3KnobId (k))) return p->getCurrentValueAsText();
    return {};
}

juce::String PadProcessor::getMidiStatus() const
{
    if (! juce::JUCEApplicationBase::isStandaloneApp()) return "PLUGIN";
    return juce::String (lpd8Seen.load() ? "LPD8 OK" : "LPD8 NOT FOUND") + "   " + (traktorSeen.load() ? "TRAKTOR CLOCK PORT OK" : "TRAKTOR CLOCK PORT ?");
}

//==============================================================================
void PadProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("BRUME_ENGINE"), nullptr);
    juce::ValueTree b ("BRUME_ENGINE");
    b.appendChild (brumeEngine.apvts.copyState(), nullptr);
    state.appendChild (b, nullptr);
    state.removeChild (state.getChildWithName ("LPD8"), nullptr);
    juce::ValueTree map ("LPD8");
    for (int p = 0; p < numProgs; ++p)
    {
        const auto& mp = maps[(size_t) p];
        juce::ValueTree e ("P" + juce::String (p));
        e.setProperty ("learned", mp.learned, nullptr); e.setProperty ("padCh", mp.padChannel, nullptr);
        e.setProperty ("note0", mp.padNote0, nullptr); e.setProperty ("knobCh", mp.knobChannel, nullptr); e.setProperty ("cc0", mp.knobCC0, nullptr);
        map.appendChild (e, nullptr);
    }
    state.appendChild (map, nullptr);
    state.setProperty ("program", program.load(), nullptr);
    state.setProperty ("ppSteps", ppSteps.load(), nullptr);
    state.setProperty ("pcBase", pcBase.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void PadProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    auto b = state.getChildWithName ("BRUME_ENGINE");
    if (b.isValid() && b.getNumChildren() > 0) brumeEngine.apvts.replaceState (b.getChild (0).createCopy());
    auto map = state.getChildWithName ("LPD8");
    for (int p = 0; p < numProgs && map.isValid(); ++p)
    {
        auto e = map.getChildWithName ("P" + juce::String (p));
        if (! e.isValid()) continue;
        auto& mp = maps[(size_t) p];
        mp.learned = e.getProperty ("learned", false); mp.padChannel = e.getProperty ("padCh", 0);
        mp.padNote0 = e.getProperty ("note0", 36); mp.knobChannel = e.getProperty ("knobCh", 0); mp.knobCC0 = e.getProperty ("cc0", 70);
    }
    program.store (juce::jlimit (0, numProgs - 1, (int) state.getProperty ("program", 0)));
    pcBase.store ((int) state.getProperty ("pcBase", 0));
    if (state.hasProperty ("ppSteps")) { ppSteps.store ((int) state.getProperty ("ppSteps")); lastPattern = -2; }
    state.removeChild (b, nullptr);
    state.removeChild (map, nullptr);
    apvts.replaceState (state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PadProcessor(); }
