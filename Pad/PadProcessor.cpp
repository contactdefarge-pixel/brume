#include "PadProcessor.h"
#include "PadEditor.h"

using namespace bpad;

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
    add (id::delay, "Delay Short/Long", 0.0f, [] (float v, int)
    {
        if (v < 0.01f) return juce::String ("OFF");
        return juce::String (v < 0.5f ? "SHORT " : "LONG ") + juce::String (juce::roundToInt (v * 100.0f)) + " %";
    });
    add (id::reverb, "Reverb", 0.0f, fxText);
    add (id::hpfEcho, "HPF Echo", 0.0f, fxText);
    add (id::lpfEcho, "LPF Echo", 0.0f, fxText);
    add (id::crushEcho, "Crush Echo", 0.0f, fxText);
    add (id::noise, "White Noise", 0.0f, fxText);
    add (id::sound, "Drum Sound", 1.0f / 3.0f, [] (float v, int) { return juce::String (soundName ((int) std::lround (v * 3.0f))); });
    ps.push_back (std::make_unique<F> (juce::ParameterID { id::bpm, 1 }, "Tempo (no sync)", juce::NormalisableRange<float> (60.0f, 200.0f, 0.1f), 124.0f,
                                       A().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " BPM"; })));
    add (id::master, "Master", 0.8f, pct);
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
    delayFx.prepare (fs, 3); hpfEcho.prepare (fs, 0); lpfEcho.prepare (fs, 1); crushEcho.prepare (fs, 2);
    reverbFx.prepare (fs); noiseWash.prepare (fs);
    noiseWash.setParameters (3.5f, 0.75f, 0.45f, true);
    noiseGen.prepare ((float) fs);
    revBuf.setSize (2, maxBlock); washBuf.setSize (2, maxBlock);
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
    if (! (m.isNoteOnOrOff() || m.isController())) return;

    const int ch = m.getChannel();
    // ---- clicks on the screen ----
    if (ch == kScreenChannel)
    {
        if (m.isController())  handleKnob (m.getControllerNumber() / 8, m.getControllerNumber() % 8, (float) m.getControllerValue() / 127.0f);
        else                   handlePad (m.getNoteNumber() / 8, m.getNoteNumber() % 8, m.isNoteOn(), m.getFloatVelocity());
        return;
    }

    // ---- LPD8 setup: learn where each program sends its pads and knobs ----
    const int step = learnStep.load();
    if (step > 0)
    {
        auto& mp = maps[(size_t) (step <= 2 ? 0 : 1)];
        if ((step == 1 || step == 3) && m.isNoteOn()) { mp.padChannel = ch; mp.padNote0 = m.getNoteNumber(); learnStep.store (step + 1); return; }
        if ((step == 2 || step == 4) && m.isController())
        {
            mp.knobChannel = ch; mp.knobCC0 = m.getControllerNumber(); mp.learned = true;
            learnStep.store (step == 4 ? 0 : step + 1);
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
            { program.store (p); handlePad (p, note - mp.padNote0, m.isNoteOn(), m.getFloatVelocity()); return; }
        }
        if (note >= 36 && note < 44) { handlePad (program.load(), note - 36, m.isNoteOn(), m.getFloatVelocity()); return; }   // not set up yet
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

    // pads 5 + 8 together = KILL FX (in any program)
    if (down && (pad == 4 || pad == 7))
    {
        const double now = (double) sampleCounter / fs;
        if (now - std::max (lastPad5Time, lastPad8Time) > 0.15) { soundBeforeCombo = apvts.getRawParameterValue (id::sound)->load(); activeBeforeCombo = activeSound.load(); }
        (pad == 4 ? lastPad5Time : lastPad8Time) = now;
        if (std::abs (lastPad5Time - lastPad8Time) < 0.15)
        {
            if (prog == 0) { setParam (apvts, id::sound, soundBeforeCombo); activeSound.store (activeBeforeCombo); }   // the combo is not a sound choice
            doKill();
            return;
        }
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

    if (prog == 1)
    {
        auto& b = brumeEngine.apvts;
        switch (pad)
        {
            case 0: if (down) setParam (b, brume::id::on, b.getRawParameterValue (brume::id::on)->load() > 0.5f ? 0.0f : 1.0f); break;
            case 1: if (down) brumeEngine.tap(); break;
            case 2: if (down) setParam (b, brume::id::tremolo, b.getRawParameterValue (brume::id::tremolo)->load() > 0.05f ? 0.0f : 0.8f); break;
            case 3: setParam (b, brume::id::on, down ? 1.0f : 0.0f); break;   // HOLD: on while the pad is held
            default:
                if (down)
                {
                    static const int div[] = { 2, 4, 6, 8 };   // 1/4 1/8 1/16 1/32 in Brume's division list
                    setParam (b, brume::id::tempo, (float) div[pad - 4] / 9.0f);
                    if (b.getRawParameterValue (brume::id::tremolo)->load() < 0.05f) setParam (b, brume::id::tremolo, 0.8f);
                }
                break;
        }
    }
}

void PadProcessor::handleKnob (int prog, int knob, float v)
{
    if (prog == 0) setParam (apvts, p1KnobId (knob), v);
    else if (prog == 1) setParam (brumeEngine.apvts, p2KnobId (knob), v);
}

void PadProcessor::doKill()
{
    for (int k = 2; k < numKnobs; ++k) fx[(size_t) k].kill();
    setParam (brumeEngine.apvts, brume::id::on, 0.0f);   // Brume stops, its tail fades with LENGTH
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
    if (revBuf.getNumSamples() < n) { revBuf.setSize (2, n, false, false, true); washBuf.setSize (2, n, false, false, true); }
    revBuf.clear(); washBuf.clear();

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
        target = (clockTicks + (double) (blockStart - lastClockSample) / clockTickSamples) / 24.0;
    }
   #if BRUME_HAS_LINK
    else if (link != nullptr && linkPeers.load() > 0)
    {
        // beat at the moment this block will be heard (block length as an estimate of the output latency)
        const auto t = link->clock().micros() + std::chrono::microseconds ((long long) (1.0e6 * n / fs));
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
    static const char* fxIds[numKnobs] = { id::drums, id::pitch, id::delay, id::reverb, id::hpfEcho, id::lpfEcho, id::crushEcho, id::noise };
    for (int k = 2; k < numKnobs; ++k)
    {
        const float pv = apvts.getRawParameterValue (fxIds[k])->load();
        if (std::abs (pv - fx[(size_t) k].target) > 1.0e-4f) fx[(size_t) k].set (pv);
        fxKilledUi[(size_t) k].store (fx[(size_t) k].killed && fx[(size_t) k].target > 0.01f);
    }

    // ---- 1) drums (X-PAD): rolls on the held speed pad, mixed into the music ----
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
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
            float y = 0.0f;
            for (auto& v : voices) if (v.active) y += v.tick();
            y *= drumGain;
            L[i] += y; R[i] += y;
            b += inc;
        }
        beat = b;
    }

    // ---- 2) program 2: Brume (noise voiced like the Revolo, reverb, tremolo), synced on the same clock ----
    brumeEngine.processBlock (buffer, clockForBrume);

    // ---- 3) program 1 effects ----
    const double spb = 60.0 / bpm * fs;   // samples per beat
    const float aT3 = fx[3].target;
    reverbFx.setParameters (1.2f + 7.0f * aT3, 0.35f + 0.5f * aT3, 0.75f);   // length follows the knob, not the kill: tails keep their length
    {
        const float d = fx[2].target;
        static const double shortBeats[] = { 1.0 / 16.0, 3.0 / 32.0, 1.0 / 8.0 }, longBeats[] = { 0.25, 0.375, 0.5, 0.75, 1.0 };
        const double beats = d < 0.5 ? shortBeats[juce::jlimit (0, 2, (int) (d / 0.5 * 3.0))] : longBeats[juce::jlimit (0, 4, (int) ((d - 0.5) / 0.5 * 5.0))];
        delayFx.line.setDelay (beats * spb);
        delayFx.setFilter (d < 0.5 ? 9000.0f : 6000.0f);
    }
    hpfEcho.line.setDelay (0.5 * spb);
    lpfEcho.line.setDelay (0.75 * spb);
    crushEcho.line.setDelay (0.25 * spb);
    hpfEcho.setFilter (150.0f + 1100.0f * fx[4].target);
    lpfEcho.setFilter (3000.0f * std::pow (0.12f, fx[5].target));
    crushEcho.setFilter (7000.0f);

    const float smoothFast = 1.0f - std::exp (-1.0f / (0.03f * (float) fs));
    const float smoothNoise = 1.0f - std::exp (-1.0f / (0.35f * (float) fs));
    auto* rl = revBuf.getWritePointer (0); auto* rr = revBuf.getWritePointer (1);
    auto* wl = washBuf.getWritePointer (0); auto* wr = washBuf.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        for (int k = 2; k < numKnobs; ++k)
            sm[k] += (fx[(size_t) k].effective() - sm[k]) * (k == 7 ? smoothNoise : smoothFast);

        float l = L[i], r = R[i];

        // HPF / LPF sweep on the music (part of the filter echoes)
        if (i % 16 == 0)
        {
            hpHz = 20.0f * std::pow (60.0f, sm[4]);   // up to 1.2 kHz: thinner, never gone
            lpHz = 20000.0f * std::pow (0.02f, sm[5]);
            for (int c = 0; c < 2; ++c) { dryHp[c].set ((float) fs, hpHz, 0.75f); dryLp[c].set ((float) fs, lpHz, 0.75f); }
        }
        if (sm[4] > 0.002f) { l = dryHp[0].tick (l).hp; r = dryHp[1].tick (r).hp; }
        if (sm[5] > 0.002f) { l = dryLp[0].tick (l).lp; r = dryLp[1].tick (r).lp; }
        dryCrush.process (l, r, std::max (0.0f, sm[6] - 0.55f) * 1.6f);

        // echoes: the knob opens the send, the wet return is fixed so tails ring out after a kill
        float el, er, o0, o1;
        delayFx.process (l * std::min (1.0f, sm[2] * 2.5f), r * std::min (1.0f, sm[2] * 2.5f), 0.25f + 0.4f * fx[2].target, 0.0f, o0, o1);
        el = o0 * 0.7f; er = o1 * 0.7f;
        hpfEcho.process (l * sm[4] * 1.6f, r * sm[4] * 1.6f, 0.5f + 0.35f * fx[4].target, 0.0f, o0, o1);
        el += o0 * 1.1f; er += o1 * 1.1f;
        lpfEcho.process (l * sm[5], r * sm[5], 0.45f + 0.35f * fx[5].target, 0.0f, o0, o1);
        el += o0 * 0.85f; er += o1 * 0.85f;
        crushEcho.process (l * sm[6], r * sm[6], 0.4f + 0.35f * fx[6].target, 0.3f + 0.7f * fx[6].target, o0, o1);
        el += o0 * 0.85f; er += o1 * 0.85f;

        // white noise (Revolo voicing) and its wash
        float nl = 0.0f, nr = 0.0f;
        if (sm[7] > 1.0e-4f) { noiseGen.tick (nl, nr); nl *= sm[7] * 0.55f; nr *= sm[7] * 0.55f; }
        wl[i] = nl * 0.8f; wr[i] = nr * 0.8f;

        // reverb send (music + echoes)
        rl[i] = (l + el * 0.5f) * sm[3] * 0.8f;
        rr[i] = (r + er * 0.5f) * sm[3] * 0.8f;

        L[i] = l + el + nl;
        R[i] = r + er + nr;
    }
    reverbFx.process (rl, rr, n);
    noiseWash.process (wl, wr, n);
    const float master = std::pow (apvts.getRawParameterValue (id::master)->load(), 2.0f) * 1.25f;
    for (int i = 0; i < n; ++i)
    {
        L[i] = std::tanh ((L[i] + rl[i] * 1.3f + wl[i] * 1.1f) * master);
        R[i] = std::tanh ((R[i] + rr[i] * 1.3f + wr[i] * 1.1f) * master);
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
    if (prog == 0) return apvts.getRawParameterValue (p1KnobId (k))->load();
    if (prog == 1) if (auto* p = brumeEngine.apvts.getParameter (p2KnobId (k))) return p->getValue();
    return 0.0f;
}

juce::String PadProcessor::getKnobText (int prog, int k) const
{
    if (prog == 0) if (auto* p = apvts.getParameter (p1KnobId (k))) return p->getCurrentValueAsText();
    if (prog == 1) if (auto* p = brumeEngine.apvts.getParameter (p2KnobId (k))) return p->getCurrentValueAsText();
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
    state.removeChild (b, nullptr);
    state.removeChild (map, nullptr);
    apvts.replaceState (state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PadProcessor(); }
