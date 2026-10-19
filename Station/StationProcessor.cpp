#include "StationProcessor.h"
#include "StationEditor.h"

using namespace station;

namespace
{
    const char* kColumnOn[4]  = { id::brumeOn, id::snareOn, id::patOn, id::padOn };
    const char* kKnobMain[4]  = { id::brumeLength, id::snareRate, id::patDensity, id::padChord };
    const char* kKnobAlt[4]   = { id::brumeChar, id::snareLevel, id::patKit, id::padTimbre };
    const char* kFaderMain[4] = { id::brumeAmount, id::snarePitch, id::patVolume, id::padOpen };
    const char* kFaderAlt[4]  = { id::brumeSend, id::snareTone, id::patVolume, id::padOpen };

    enum Action { aSnare = 1, aPattern, aPad, aBuild, aShot, aSwell, aDrop, aKill };

    inline float lengthSeconds (float v) { return 0.3f * std::pow (40.0f, v); }

    int mask (std::initializer_list<int> steps) { int m = 0; for (int s : steps) m |= 1 << s; return m; }

    float positiveMod (double x, double m) { const double r = std::fmod (x, m); return (float) (r < 0 ? r + m : r); }
}

//==============================================================================
StationProcessor::StationProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pBrumeOn = p (id::brumeOn); pBrumeAmount = p (id::brumeAmount); pBrumeLength = p (id::brumeLength);
    pBrumeSend = p (id::brumeSend); pBrumeChar = p (id::brumeChar);
    pSnareOn = p (id::snareOn); pSnareRate = p (id::snareRate); pSnarePitch = p (id::snarePitch);
    pSnareLevel = p (id::snareLevel); pSnareTone = p (id::snareTone);
    pPatOn = p (id::patOn); pPatVolume = p (id::patVolume); pPatDensity = p (id::patDensity); pPatKit = p (id::patKit);
    pPadOn = p (id::padOn); pPadOpen = p (id::padOpen); pPadChord = p (id::padChord); pPadTimbre = p (id::padTimbre);
    pKey = p (id::key); pQuant = p (id::quant); pBpm = p (id::bpm); pMaster = p (id::master);

    for (int s = 0; s < numSlots; ++s) loadPreset (s);
    Rng r; r.s = 0x51ed27u;
    for (auto& t : hashTable) for (auto& h : t) h = (r.next() + 1.0f) * 0.5f;
    for (auto& a : armed) a.store (false);
    for (auto& c : padRgb) c.store (0);
    for (auto& t : padHitTime) t.store (-10.0);

    if (juce::JUCEApplicationBase::isStandaloneApp())
        f1Reader.start();
    startTimerHz (30);
}

StationProcessor::~StationProcessor() { stopTimer(); }

juce::AudioProcessorValueTreeState::ParameterLayout StationProcessor::createLayout()
{
    using F = juce::AudioParameterFloat; using B = juce::AudioParameterBool; using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
    const juce::NormalisableRange<float> r (0.0f, 1.0f);
    auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
    auto add = [&] (const char* pid, const char* name, float def, std::function<juce::String (float, int)> txt)
    {
        ps.push_back (std::make_unique<F> (juce::ParameterID { pid, 1 }, name, r, def, A().withStringFromValueFunction (txt)));
    };
    auto onOff = [&] (const char* pid, const char* name, bool def) { ps.push_back (std::make_unique<B> (juce::ParameterID { pid, 1 }, name, def)); };

    onOff (id::brumeOn, "Brume On", false);
    add (id::brumeAmount, "Brume Noise", 0.5f, pct);
    add (id::brumeLength, "Brume Length", 0.55f, [] (float v, int) { return juce::String (lengthSeconds (v), 1) + " s"; });
    add (id::brumeSend, "Brume Send", 0.5f, pct);
    add (id::brumeChar, "Brume Character", 0.5f, pct);

    onOff (id::snareOn, "Snare On", false);
    add (id::snareRate, "Snare Speed", 0.2f, [] (float v, int) { return juce::String (snareRate (v).label); });
    add (id::snarePitch, "Snare Pitch", 1.0f / 3.0f, [] (float v, int) { const float s = snareSemis (v); return (s > 0 ? "+" : "") + juce::String (s, 1) + " st"; });
    add (id::snareLevel, "Snare Level", 0.8f, pct);
    add (id::snareTone, "Snare Tone", 0.5f, pct);

    onOff (id::patOn, "Patterns On", false);
    add (id::patVolume, "Patterns Volume", 0.7f, pct);
    add (id::patDensity, "Patterns Density", 0.5f, pct);
    add (id::patKit, "Patterns Kit", 0.5f, pct);

    onOff (id::padOn, "Pad On", false);
    add (id::padOpen, "Pad Open", 0.45f, pct);
    add (id::padChord, "Pad Chord", 0.0f, [] (float v, int) { return "Degree " + juce::String (chordDegree (v) + 1); });
    add (id::padTimbre, "Pad Timbre", 0.4f, pct);
    juce::StringArray keys;
    for (int k = 0; k < 24; ++k) keys.add (keyName (k));
    ps.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::key, 1 }, "Key", keys, 7));   // 8A = A minor

    onOff (id::quant, "Quantize", true);
    ps.push_back (std::make_unique<F> (juce::ParameterID { id::bpm, 1 }, "Tempo (no sync)", juce::NormalisableRange<float> (60.0f, 200.0f, 0.1f), 124.0f,
                                       A().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " BPM"; })));
    add (id::master, "Master", 0.8f, pct);
    return { ps.begin(), ps.end() };
}

bool StationProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorEditor* StationProcessor::createEditor() { return new StationEditor (*this); }

void StationProcessor::loadPreset (int s)
{
    // 0 kick, 1 clap, 2 closed hat, 3 open hat, 4 perc
    static const std::array<std::array<int, numTracks>, numSlots> presets {{
        {{ mask ({ 0, 4, 8, 12 }), mask ({ 4, 12 }), mask ({ 0, 3, 7, 11, 15 }), mask ({ 2, 6, 10, 14 }), 0 }},                         // house
        {{ mask ({ 0, 4, 8, 12 }), mask ({ 12 }), 0xffff, mask ({ 2, 6, 10, 14 }), mask ({ 7, 15 }) }},                                  // techno
        {{ mask ({ 0, 10 }), mask ({ 4, 12 }), mask ({ 0, 2, 4, 6, 8, 10, 12, 14 }), mask ({ 14 }), mask ({ 7, 13 }) }},               // breaks
        {{ mask ({ 0, 4, 8, 12 }), 0, mask ({ 2, 6, 10, 14 }), 0, mask ({ 3, 6, 11, 13 }) }}                                              // minimal
    }};
    for (int t = 0; t < numTracks; ++t)
        pattern[(size_t) s][(size_t) t].store (presets[(size_t) s][(size_t) t]);
}

//==============================================================================
void StationProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxBlock = std::max (32, samplesPerBlock);
    sendBuf.setSize (2, maxBlock);
    reverb.prepare (fs);
    pad.prepare ((float) fs);
    for (int c = 0; c < 2; ++c) { noiseHP[c].set ((float) fs, 35.0f, 0.7f); noiseLP[c].set ((float) fs, (float) std::min (16000.0, fs * 0.45), 0.7f); }
    controlQueue.reset (fs);
    queueReady.store (true);
    lastKey = lastDegree = -1;
}

void StationProcessor::sendControl (const juce::MidiMessage& m)
{
    if (! queueReady.load()) return;
    auto msg = m;
    msg.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    controlQueue.addMessageToQueue (msg);
}

void StationProcessor::setParam (const char* pid, float v)
{
    if (auto* prm = apvts.getParameter (pid))
        if (std::abs (prm->getValue() - v) > 1.0e-4f)
            prm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
}

void StationProcessor::toggleParam (const char* pid)
{
    if (auto* prm = apvts.getParameter (pid))
        prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
}

double StationProcessor::nextBeat (double tol) const
{
    const double bb = beat - barOffset;
    const double fr = bb - std::floor (bb);
    return fr < tol ? bb : std::floor (bb) + 1.0;   // a slightly late press still lands on the beat just played
}

double StationProcessor::nextBar (double tol) const
{
    const double bb = beat - barOffset;
    const double pos = positiveMod (bb, 4.0);
    return pos < tol ? bb : bb - pos + 4.0;
}

void StationProcessor::schedule (double at, int action, int arg)
{
    if (at <= beat - barOffset + 1.0e-9) { runAction (action, arg); return; }
    for (auto& pd : pending)
        if (! pd.used) { pd = { at, action, arg, true }; return; }
}

//==============================================================================
void StationProcessor::handleMidi (const juce::MidiMessage& m, juce::int64 samplePos)
{
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
        return;
    }
    if (m.isMidiStart())           { clockTicks = -1.0; barOffset = 0.0; return; }
    if (m.isSongPositionPointer()) { clockTicks = m.getSongPositionPointerMidiBeat() * 6.0 - 1.0; return; }
    if (m.getChannel() != F1Reader::channel) return;

    if (m.isController())   handleControl (1, m.getControllerNumber(), m.getControllerValue());
    else if (m.isNoteOn())  handleControl (2, m.getNoteNumber(), 127);
    else if (m.isNoteOff()) handleControl (2, m.getNoteNumber(), 0);
}

void StationProcessor::handleControl (int type, int num, int val)
{
    lastEventCode.store ((type << 8) | num);
    const bool shift = shiftHeld.load();

    if (type == 2)
    {
        if (num == 24) { shiftHeld.store (val > 0); return; }
        if (val == 0) return;   // act on the press
        if (num >= 1 && num <= 16)  { pressPad (num - 1); return; }
        if (num >= 17 && num <= 20) { toggleParam (kColumnOn[num - 17]); return; }
        switch (num)
        {
            case 21: {   // SYNC: the current beat becomes beat 1 of the bar
                barOffset = positiveMod (std::round (beat), 4.0);
                lastStepIdx = (juce::int64) std::floor ((beat - barOffset) * 4.0) - 1;
                break;
            }
            case 22: toggleParam (id::quant); break;
            case 25: runAction (aSwell, 1); break;
            case 26: stepMode.store (! stepMode.load()); break;
            case 28: pressPad (14); break;   // BROWSE = tap tempo
            case 29: if (stepMode.load()) selTrack.store ((selTrack.load() + 1) % numTracks); break;
            default: break;
        }
        return;
    }

    const float v = (float) val / 127.0f;
    if (num >= 1 && num <= 4)      setParam ((shift ? kKnobAlt : kKnobMain)[num - 1], v);
    else if (num >= 5 && num <= 8) setParam ((shift ? kFaderAlt : kFaderMain)[num - 5], v);
    else if (num == 9)
    {
        const int dir = val < 64 ? 1 : -1;
        if (stepMode.load())
        {
            if (shift) slot.store ((slot.load() + dir + numSlots) % numSlots);
            else       selTrack.store ((selTrack.load() + dir + numTracks) % numTracks);
        }
        else if (auto* kp = apvts.getParameter (id::key))
        {
            const int k = (int) pKey->load();
            const int half = k / 12, n = k % 12;
            const int nk = shift ? (k + 12) % 24 : half * 12 + (n + dir + 12) % 12;   // SHIFT: minor <-> major
            kp->setValueNotifyingHost (kp->convertTo0to1 ((float) nk));
        }
    }
}

void StationProcessor::pressPad (int pIdx)
{
    padHitTime[(size_t) pIdx].store (uiTime());
    if (stepMode.load())
    {
        auto& cell = pattern[(size_t) slot.load()][(size_t) selTrack.load()];
        if (shiftHeld.load())
        {
            if (pIdx < numSlots)  slot.store (pIdx);
            else if (pIdx == 12)  cell.store (0);
            else if (pIdx == 15)  loadPreset (slot.load());
            return;
        }
        cell.store (cell.load() ^ (1 << pIdx));
        return;
    }

    const int row = pIdx / 4, col = pIdx % 4;
    const bool q = pQuant->load() > 0.5f;
    switch (row)
    {
        case 0: { static const int bars[] = { 2, 4, 8, 16 }; schedule (nextBar(), aBuild, bars[col]); break; }
        case 1:
            if (col < 3) { if (q) schedule (nextBeat(), aShot, col); else runAction (aShot, col); }
            else runAction (aSwell, 1);
            break;
        case 2: { static const int deg[] = { 0, 3, 4, 5 }; setParam (id::padChord, (float) deg[col] / 6.0f); break; }
        default:
            if (col == 0) runAction (aDrop, 0);
            else if (col == 1) runAction (aKill, 0);
            else if (col == 3) runAction (aSwell, 2);
            else if (syncSource.load() == 0)   // TAP: sets the internal tempo when nothing syncs Station
            {
                const double now = (double) sampleCounter / fs;
                for (int i = taps.size(); --i >= 0;) if (now - taps[i] > 2.0) taps.remove (i);
                taps.add (now);
                if (taps.size() >= 2)
                {
                    const double avg = (taps.getLast() - taps[0]) / (taps.size() - 1);
                    if (auto* bp = apvts.getParameter (id::bpm))
                        bp->setValueNotifyingHost (bp->convertTo0to1 ((float) juce::jlimit (60.0, 200.0, 60.0 / avg)));
                }
                if (taps.size() > 6) taps.remove (0);
            }
            break;
    }
}

void StationProcessor::runAction (int action, int arg)
{
    const double bb = beat - barOffset;
    switch (action)
    {
        case aSnare:
            running[1] = true; armed[1].store (false);
            lastRatePerBeat = snareRate (pSnareRate->load()).perBeat;
            lastSnareIdx = (juce::int64) std::floor (bb * lastRatePerBeat) - 1;
            break;
        case aPattern:
            running[2] = true; armed[2].store (false);
            lastStepIdx = (juce::int64) std::floor (bb * 4.0) - 1;
            break;
        case aPad:
            running[3] = true; armed[3].store (false);
            break;
        case aBuild:
            buildActive = true; buildStart = bb; buildLen = 4.0 * arg;
            lastRatePerBeat = 1.0f;
            lastSnareIdx = (juce::int64) std::floor (bb) - 1;
            break;
        case aShot:
            shots[(size_t) nextShot].trigger ((float) fs, arg);
            nextShot = (nextShot + 1) % (int) shots.size();
            break;
        case aSwell:
        {
            double remain = (bb - positiveMod (bb, 4.0) + 4.0) - bb;
            if (remain < 1.0) remain += 4.0;
            if (arg == 2) remain += 4.0;
            shots[(size_t) nextShot].trigger ((float) fs, 3, (float) (remain * 60.0 / std::max (40.0f, bpmUi.load())));
            nextShot = (nextShot + 1) % (int) shots.size();
            break;
        }
        case aDrop:
        case aKill:
            buildActive = false;
            for (auto& pd : pending) pd.used = false;
            for (int c = 0; c < 4; ++c) { running[c] = false; armed[(size_t) c].store (false); setParam (kColumnOn[c], 0.0f); prevOn[c] = false; }
            if (action == aKill)
            {
                for (auto& v : snares) v.active = false;
                for (auto& v : drums) v.active = false;
                for (auto& v : shots) v.active = false;
                reverb.clear();
                noiseGate = 0.0f; pad.gain = 0.0f;
            }
            break;
        default: break;
    }
}

void StationProcessor::triggerSnare (float vel, float extraSemis)
{
    snares[(size_t) nextSnare].trigger ((float) fs, snareSemis (pSnarePitch->load()) + extraSemis, pSnareTone->load(), vel,
                                        (juce::uint32) sampleCounter);
    nextSnare = (nextSnare + 1) % (int) snares.size();
}

void StationProcessor::triggerDrum (int track, float vel)
{
    if (track == 2)   // a closed hat chokes the open hat
        for (auto& d : drums) if (d.active && d.type == 3) d.active = false;
    drums[(size_t) nextDrum].trigger ((float) fs, track, vel, pPatKit->load());
    nextDrum = (nextDrum + 1) % (int) drums.size();
}

void StationProcessor::stepTick (int step)
{
    playStep.store (step);
    const float d = pPatDensity->load();
    static const float addWeight[numTracks] = { 0.18f, 0.15f, 1.0f, 0.3f, 0.6f };
    const int s = slot.load();
    for (int t = 0; t < numTracks; ++t)
    {
        const bool on = (pattern[(size_t) s][(size_t) t].load() >> step) & 1;
        const float h = hashTable[t][step];
        float vel = 0.0f;
        if (on)
        {
            if (h >= std::max (0.0f, (0.5f - d) * 2.0f)) vel = (t == 2 && (step & 1)) ? 0.7f : 1.0f;
        }
        else if (d > 0.5f && h < (d - 0.5f) * 2.0f * addWeight[t])
        {
            vel = 0.5f;
        }
        if (vel > 0.0f) triggerDrum (t, vel);
    }
}

//==============================================================================
void StationProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0) return;
    if (sendBuf.getNumSamples() < n) sendBuf.setSize (2, n, false, false, true);
    sendBuf.clear();

    const juce::int64 blockStart = sampleCounter;
    for (const auto meta : midi) handleMidi (meta.getMessage(), blockStart + meta.samplePosition);
    midi.clear();
    {
        juce::MidiBuffer ctrl;
        controlQueue.removeNextBlockOfMessages (ctrl, n);
        for (const auto meta : ctrl) handleMidi (meta.getMessage(), blockStart);
    }

    // ---- tempo: Traktor's MIDI clock first, then the host (Ableton), else the internal tempo ----
    int src = 0;
    double bpm = pBpm->load(), target = 0.0;
    if (clockTickSamples > 0.0 && lastClockSample >= 0 && blockStart - lastClockSample < (juce::int64) (0.5 * fs))
    {
        src = 1;
        bpm = fs * 60.0 / (clockTickSamples * 24.0);
        target = (clockTicks + (double) (blockStart - lastClockSample) / clockTickSamples) / 24.0;
    }
    else if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
            if (pos->getIsPlaying() && pos->getBpm() && pos->getPpqPosition())
            {
                src = 2; bpm = *pos->getBpm(); target = *pos->getPpqPosition();
            }
    }
    bpm = juce::jlimit (40.0, 250.0, bpm);
    if (src != 0)
    {
        const double diff = target - beat;
        if (std::abs (diff) > 0.5 || syncSource.load() != src)
        {
            beat = target;
            lastStepIdx = (juce::int64) std::floor ((beat - barOffset) * 4.0) - (running[2] ? 0 : 1);
            lastSnareIdx = (juce::int64) std::floor ((beat - barOffset) * std::max (1.0f, lastRatePerBeat));
        }
        else beat += diff * 0.2;   // follow the clock smoothly, never jump backwards over a step
    }
    syncSource.store (src);
    const double inc = bpm / 60.0 / fs;

    // ---- columns: start on the grid, stop at once ----
    const bool q = pQuant->load() > 0.5f;
    std::atomic<float>* onParams[4] = { pBrumeOn, pSnareOn, pPatOn, pPadOn };
    for (int c = 0; c < 4; ++c)
    {
        const bool on = onParams[c]->load() > 0.5f;
        if (on && ! prevOn[c])
        {
            if (c == 0)                    running[0] = true;
            else if (! q)                  runAction (c == 1 ? aSnare : c == 2 ? aPattern : aPad, 0);
            else { armed[(size_t) c].store (true); schedule (c == 2 ? nextBar() : nextBeat(), c == 1 ? aSnare : c == 2 ? aPattern : aPad, 0); }
        }
        else if (! on && prevOn[c])
        {
            running[c] = false; armed[(size_t) c].store (false);
            const int a = c == 1 ? aSnare : c == 2 ? aPattern : aPad;
            for (auto& pd : pending) if (pd.used && pd.action == a) pd.used = false;
        }
        prevOn[c] = on;
    }

    // ---- chord ----
    const int key = juce::jlimit (0, 23, (int) pKey->load());
    const int degree = chordDegree (pPadChord->load());
    if (key != lastKey || degree != lastDegree) { pad.setChord (chordNotes (key, degree)); lastKey = key; lastDegree = degree; }

    // ---- levels ----
    reverb.setParameters (lengthSeconds (pBrumeLength->load()), pBrumeChar->load(), 0.55f);
    const float snareLvl = std::pow (pSnareLevel->load(), 2.0f) * 0.5f;
    const float drumLvl = std::pow (pPatVolume->load(), 2.0f) * 0.85f;
    const float noiseAmt = std::pow (pBrumeAmount->load(), 0.8f) * 0.42f;
    const float sendTarget = pBrumeSend->load();
    const float masterTarget = std::pow (pMaster->load(), 2.0f) * 1.3f;
    const float padOpen = pPadOpen->load(), padTimbre = pPadTimbre->load();
    const float cNoiseOn = 1.0f - std::exp (-1.0f / (0.02f * (float) fs));
    const float cNoiseOff = 1.0f - std::exp (-1.0f / (lengthSeconds (pBrumeLength->load()) / 6.9f * (float) fs));
    const float padFade = running[3] ? 1.0f - std::exp (-1.0f / (1.2f * (float) fs)) : 1.0f - std::exp (-1.0f / (1.8f * (float) fs));
    const float smooth = 1.0f - std::exp (-1.0f / (0.03f * (float) fs));

    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (1);
    auto* sL = sendBuf.getWritePointer (0);
    auto* sR = sendBuf.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        const double bb = beat - barOffset;

        for (auto& pd : pending)
            if (pd.used && bb >= pd.at) { pd.used = false; runAction (pd.action, pd.arg); }

        // patterns: one step per 16th
        const auto stepIdx = (juce::int64) std::floor (bb * 4.0);
        if (running[2])
        {
            if (stepIdx > lastStepIdx || stepIdx < lastStepIdx - 2) { stepTick ((int) (((stepIdx % 16) + 16) % 16)); lastStepIdx = stepIdx; }
        }
        else lastStepIdx = stepIdx;

        // snare: manual (knob = speed, fader = pitch) or automatic build
        float rate = 0.0f, extra = 0.0f, vel = 1.0f;
        if (buildActive)
        {
            const double f = (bb - buildStart) / buildLen;
            if (f >= 1.0)
            {
                buildActive = false;   // the build ends exactly on the drop
                running[1] = false;
                setParam (id::snareOn, 0.0f); prevOn[1] = false;
            }
            else
            {
                rate = f < 0.5 ? 1.0f : f < 0.75 ? 2.0f : f < 0.875 ? 4.0f : 8.0f;
                extra = (float) (12.0 * f);
                vel = (float) (0.45 + 0.55 * f);
            }
        }
        else if (running[1])
        {
            rate = snareRate (pSnareRate->load()).perBeat;
        }
        if (rate > 0.0f)
        {
            if (rate != lastRatePerBeat) { lastSnareIdx = (juce::int64) std::floor (bb * rate); lastRatePerBeat = rate; }
            const auto si = (juce::int64) std::floor (bb * rate);
            if (si > lastSnareIdx) { triggerSnare (vel, extra); lastSnareIdx = si; }
        }

        // ---- render ----
        float l = 0.0f, r = 0.0f, send = 0.0f;
        float sn = 0.0f;
        for (auto& v : snares) sn += v.tick();
        sn *= snareLvl;
        l += sn; r += sn; send += sn * 0.25f;

        for (auto& v : drums)
        {
            if (! v.active) continue;
            const float y = v.tick() * drumLvl;
            const float pan = v.type == 2 ? 0.62f : v.type == 3 ? 0.58f : v.type == 4 ? 0.4f : 0.5f;
            l += y * (1.0f - pan) * 2.0f * 0.7071f * 1.0f; r += y * pan * 2.0f * 0.7071f;
            send += y * (v.type == 1 ? 0.12f : 0.04f);
        }

        float sh = 0.0f, shSend = 0.0f;
        for (auto& v : shots)
        {
            if (! v.active) continue;
            const float y = v.tick() * 0.8f;
            sh += y; shSend += y * (v.type == 0 ? 0.6f : 0.4f);
        }
        l += sh; r += sh; send += shSend;

        float pl, pr;
        pad.tick (running[3] ? 1.0f : 0.0f, padFade, padOpen, padTimbre, pl, pr);
        l += pl; r += pr;

        // Brume: noise, gated by its MUTE with a natural fade (Length)
        noiseGate += ((running[0] ? 1.0f : 0.0f) - noiseGate) * (running[0] ? cNoiseOn : cNoiseOff);
        float nl = 0.0f, nr = 0.0f;
        if (noiseGate > 1.0e-5f)
        {
            const float common = noiseRng.next();
            const float a = (common * 0.71f + noiseRng.next() * 0.71f) * 0.5f;
            const float b = (common * 0.71f + noiseRng.next() * 0.71f) * 0.5f;
            const float g = noiseAmt * noiseGate;
            nl = noiseLP[0].tick (noiseHP[0].tick (a).hp).lp * g;
            nr = noiseLP[1].tick (noiseHP[1].tick (b).hp).lp * g;
        }
        l += nl * 0.85f; r += nr * 0.85f;

        sendSm += (sendTarget - sendSm) * smooth;
        const float sendAmt = sendSm * 2.0f;
        sL[i] = (send + pl * 0.3f) * sendAmt + nl * 0.6f;
        sR[i] = (send + pr * 0.3f) * sendAmt + nr * 0.6f;
        outL[i] = l; outR[i] = r;

        beat += inc;
    }

    // ---- Brume reverb on the send bus, then master ----
    reverb.process (sL, sR, n);
    for (int i = 0; i < n; ++i)
    {
        masterSm += (masterTarget - masterSm) * smooth;
        outL[i] = std::tanh ((outL[i] + sL[i] * 1.3f) * masterSm);
        outR[i] = std::tanh ((outR[i] + sR[i] * 1.3f) * masterSm);
    }

    sampleCounter += n;
    bpmUi.store ((float) bpm);
    barBeatUi.store (positiveMod (beat - barOffset, 4.0));
    buildLeftUi.store (buildActive ? (float) (buildStart + buildLen - (beat - barOffset)) : -1.0f);
    if (! running[2]) playStep.store (-1);
}

//==============================================================================
float StationProcessor::padFlash (int pIdx) const
{
    const double dt = uiTime() - padHitTime[(size_t) pIdx].load();
    return dt < 0.0 || dt > 0.25 ? 0.0f : (float) (1.0 - dt / 0.25);
}

juce::String StationProcessor::getDisplayText() const
{
    const float left = buildLeftUi.load();
    if (left >= 0.0f) return juce::String ((int) std::ceil (left));
    if (stepMode.load()) return juce::String (selTrack.load() + 1);
    const int k = (int) apvts.getRawParameterValue (id::key)->load();
    return juce::String (k % 12 + 1) + (k < 12 ? "A" : "B");
}

juce::String StationProcessor::getLastEvent() const
{
    const int code = lastEventCode.load();
    if (code < 0) return {};
    return F1Reader::controlName (code >> 8, code & 0xff);
}

void StationProcessor::timerCallback()
{
    buildLeds();
}

void StationProcessor::buildLeds()
{
    auto rgb = [] (float r, float g, float b) { return ((juce::uint32) juce::jlimit (0, 255, (int) (r * 255)) << 16) | ((juce::uint32) juce::jlimit (0, 255, (int) (g * 255)) << 8) | (juce::uint32) juce::jlimit (0, 255, (int) (b * 255)); };
    auto scale = [] (juce::uint32 c, float k) { return ((juce::uint32) std::min (255.0f, ((c >> 16) & 255) * k) << 16) | ((juce::uint32) std::min (255.0f, ((c >> 8) & 255) * k) << 8) | (juce::uint32) std::min (255.0f, (c & 255) * k); };

    const float bb = barBeatUi.load();
    const bool onBeat = (bb - std::floor (bb)) < 0.2f;
    const bool fastBlink = std::fmod (uiTime() * 6.0, 1.0) < 0.5;
    static const juce::uint32 trackCol[numTracks] = { 0xff3030, 0xff8a1f, 0xffd830, 0x30d8ff, 0xb050ff };

    for (int pI = 0; pI < 16; ++pI)
    {
        juce::uint32 c = 0;
        const int row = pI / 4, col = pI % 4;
        if (stepMode.load())
        {
            const int t = selTrack.load(), s = slot.load();
            if (shiftHeld.load())
            {
                if (pI < numSlots) c = pI == s ? 0xffffff : 0x303030;
                else if (pI == 12) c = 0xa00000;
                else if (pI == 15) c = 0xa05000;
            }
            else
            {
                const bool on = stepIsOn (s, t, pI);
                c = on ? trackCol[t] : scale (trackCol[t], 0.06f);
                if (pI == playStep.load()) c = on ? 0xffffff : 0x505050;
            }
        }
        else
        {
            switch (row)
            {
                case 0:
                {
                    static const float bars[] = { 2, 4, 8, 16 };
                    c = 0x402800;
                    if (buildLeftUi.load() >= 0.0f && std::abs (buildLen - bars[col] * 4.0f) < 0.1f) c = onBeat ? 0xffb000 : 0x805000;
                    break;
                }
                case 1:  c = col == 3 ? 0x301040 : 0x303030; break;
                case 2:
                {
                    static const int deg[] = { 0, 3, 4, 5 };
                    c = chordDegree (apvts.getRawParameterValue (id::padChord)->load()) == deg[col] ? 0x40a0ff : 0x081830;
                    break;
                }
                default:
                    c = col == 0 ? 0xff1010 : col == 1 ? 0x400000 : col == 2 ? (onBeat ? 0x30ff40 : 0x083010) : 0x301040;
                    break;
            }
        }
        const float fl = padFlash (pI);
        if (fl > 0.0f) c = scale (c, 1.0f + 3.0f * fl) | (fl > 0.5f ? 0x404040u : 0u);
        padRgb[(size_t) pI].store (c);
    }
    juce::ignoreUnused (rgb);

    if (f1Reader.getStatus() != F1Reader::connected) return;

    // ---- F1 light report ----
    juce::uint8 rep[F1Reader::ledReportSize] {};
    rep[0] = 0x80;
    // 7-segment: segment order in the report = g, c, b, a, f, e, d (from the Mixxx mapping)
    static const juce::uint8 digits[10] = { 0x7e, 0x06, 0x6d, 0x4f, 0x17, 0x5b, 0x7b, 0x0e, 0x7f, 0x5f };
    // bit i set = segment index i on: index 0 g, 1 c, 2 b, 3 a, 4 f, 5 e, 6 d
    auto putDigit = [&] (int base, int d, bool dp)
    {
        rep[base] = dp ? 0x7f : 0;
        if (d < 0) return;
        for (int i = 0; i < 7; ++i) rep[base + 1 + i] = (digits[d] >> i) & 1 ? 0x7f : 0;
    };
    {
        int value = 0; bool dp = false;
        const float left = buildLeftUi.load();
        if (left >= 0.0f) value = std::min (99, (int) std::ceil (left));
        else if (stepMode.load()) value = selTrack.load() + 1;
        else { const int k = (int) apvts.getRawParameterValue (id::key)->load(); value = k % 12 + 1; dp = k >= 12; }
        putDigit (1, value % 10, dp);                       // right digit (dot = major key)
        putDigit (9, value >= 10 ? value / 10 : -1, false); // left digit
    }
    rep[17] = fastBlink && onBeat ? 0x7f : 0x10;                                      // browse (tap)
    rep[19] = stepMode.load() ? 0x7f : 0x08;                                          // type
    rep[20] = 0x10;                                                                   // reverse (swell)
    rep[21] = shiftHeld.load() ? 0x7f : 0x08;                                         // shift
    rep[23] = apvts.getRawParameterValue (id::quant)->load() > 0.5f ? 0x7f : 0x08;   // quant
    rep[24] = bb < 0.25f ? 0x7f : 0x08;                                               // sync: flashes on the downbeat
    for (int pI = 0; pI < 16; ++pI)
    {
        const auto c = padRgb[(size_t) pI].load();
        rep[25 + 3 * pI]     = (juce::uint8) ((c & 255) >> 1);           // blue
        rep[25 + 3 * pI + 1] = (juce::uint8) (((c >> 16) & 255) >> 1);   // red
        rep[25 + 3 * pI + 2] = (juce::uint8) (((c >> 8) & 255) >> 1);    // green
    }
    std::atomic<float>* onParams[4] = { pBrumeOn, pSnareOn, pPatOn, pPadOn };
    for (int c = 0; c < 4; ++c)
    {
        const bool on = onParams[c]->load() > 0.5f;
        const juce::uint8 v = armed[(size_t) c].load() ? (fastBlink ? 0x7f : 0x00) : on ? 0x7f : 0x0a;
        const int off = 79 - 2 * c;   // play_1 = 79/80 ... play_4 = 73/74
        rep[off] = rep[off + 1] = v;
    }
    f1Reader.setLeds (rep);
}

//==============================================================================
void StationProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::ValueTree pats ("PATTERNS");
    pats.setProperty ("slot", slot.load(), nullptr);
    for (int s = 0; s < numSlots; ++s)
        for (int t = 0; t < numTracks; ++t)
            pats.setProperty ("s" + juce::String (s) + "t" + juce::String (t), pattern[(size_t) s][(size_t) t].load(), nullptr);
    state.removeChild (state.getChildWithName ("PATTERNS"), nullptr);
    state.appendChild (pats, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void StationProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    auto pats = state.getChildWithName ("PATTERNS");
    if (pats.isValid())
    {
        slot.store (juce::jlimit (0, numSlots - 1, (int) pats.getProperty ("slot", 0)));
        for (int s = 0; s < numSlots; ++s)
            for (int t = 0; t < numTracks; ++t)
            {
                const auto key = "s" + juce::String (s) + "t" + juce::String (t);
                if (pats.hasProperty (key)) pattern[(size_t) s][(size_t) t].store ((int) pats.getProperty (key));
            }
        state.removeChild (pats, nullptr);
    }
    apvts.replaceState (state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StationProcessor(); }
