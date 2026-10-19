#include "StationProcessor.h"
#include "StationEditor.h"

using namespace station;

namespace
{
    const char* kOnIds[4]    = { id::brumeOn, id::rollOn, id::patOn, id::padOn };
    const char* kLevelIds[4] = { id::brumeLevel, id::rollLevel, id::patLevel, id::padLevel };

    enum Action { aStartPattern = 1, aStartPad, aBuild, aShot, aSwell, aRiser, aDrop, aKill };
    enum Bus { busRoll = 0, busDrums = 1 };

    int mask (std::initializer_list<int> steps) { int m = 0; for (int s : steps) m |= 1 << s; return m; }
    double positiveMod (double x, double m) { const double r = std::fmod (x, m); return r < 0 ? r + m : r; }

    // bipolar filter knob: < 0.5 low-pass, > 0.5 high-pass, centre = open
    void setBipolar (Svf* f, float fs, float v, bool& isHp, bool& bypass)
    {
        bypass = std::abs (v - 0.5f) < 0.03f;
        isHp = v > 0.5f;
        const float hz = isHp ? 20.0f * std::pow (250.0f, (v - 0.53f) / 0.47f) : 20000.0f * std::pow (0.012f, (0.47f - v) / 0.47f);
        for (int c = 0; c < 2; ++c) f[c].set (fs, juce::jlimit (20.0f, 18000.0f, hz), 0.9f);
    }
}

//==============================================================================
StationProcessor::StationProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (int s = 0; s < numSlots; ++s) loadPreset (s);
    Rng r; r.s = 0x51ed27u;
    for (auto& t : hashTable) for (auto& h : t) h = (r.next() + 1.0f) * 0.5f;
    for (auto& a : armed) a.store (false);
    for (auto& c : padRgb) c.store (0);
    for (auto& t : padHitTime) t.store (-10.0);
    for (auto& d : padDown) d.store (false);

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
    auto bip = [] (float v, int) { return std::abs (v - 0.5f) < 0.03f ? juce::String ("OPEN") : v < 0.5f ? "LP " + juce::String (juce::roundToInt ((0.5f - v) * 200)) + " %" : "HP " + juce::String (juce::roundToInt ((v - 0.5f) * 200)) + " %"; };
    auto add = [&] (const char* pid, const char* name, float def, std::function<juce::String (float, int)> txt)
    { ps.push_back (std::make_unique<F> (juce::ParameterID { pid, 1 }, name, r, def, A().withStringFromValueFunction (txt))); };
    auto onOff = [&] (const char* pid, const char* name, bool def) { ps.push_back (std::make_unique<B> (juce::ParameterID { pid, 1 }, name, def)); };

    // global: STOP buttons and faders
    onOff (id::brumeOn, "Brume On", false); onOff (id::rollOn, "Roll On", true);
    onOff (id::patOn, "Patterns On", false); onOff (id::padOn, "Pad On", false);
    add (id::brumeLevel, "Brume Volume", 0.5f, pct); add (id::rollLevel, "Roll Volume", 0.8f, pct);
    add (id::patLevel, "Patterns Volume", 0.7f, pct); add (id::padLevel, "Pad Volume", 0.6f, pct);
    juce::StringArray keys;
    for (int k = 0; k < 24; ++k) keys.add (keyName (k));
    ps.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::key, 1 }, "Key", keys, 7));
    onOff (id::quant, "Quantize", true);
    ps.push_back (std::make_unique<F> (juce::ParameterID { id::bpm, 1 }, "Tempo (no sync)", juce::NormalisableRange<float> (60.0f, 200.0f, 0.1f), 124.0f,
                                       A().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " BPM"; })));
    add (id::master, "Master", 0.8f, pct);

    // BRUME
    add (id::brumeLength, "Brume Length", 0.55f, [] (float v, int) { return juce::String (lengthSeconds (v), 1) + " s"; });
    add (id::brumeChar, "Brume Character", 0.5f, pct);
    add (id::brumeAir, "Brume Air", 0.5f, pct);
    add (id::brumeTrem, "Brume Tremolo", 0.0f, pct);
    add (id::brumeTremRate, "Brume Trem Rate", 2.0f / 6.0f, [] (float v, int) { return juce::String (tremDiv (v).label); });
    add (id::brumeLowCut, "Brume Low Cut", 0.0f, [] (float v, int) { return juce::String (juce::roundToInt (20.0f * std::pow (60.0f, v))) + " Hz"; });
    add (id::brumeSend, "Brume Send", 0.5f, pct);
    add (id::brumeColor, "Brume Noise Color", 0.8f, pct);

    // ROLL
    add (id::rollPitch, "Roll Pitch", 0.5f, [] (float v, int) { const float s = rollSemis (v); return (s > 0 ? "+" : "") + juce::String (juce::roundToInt (s)) + " st"; });
    add (id::rollDecay, "Roll Decay", 1.0f, pct);
    add (id::rollFilter, "Roll Filter", 0.5f, bip);
    add (id::rollSend, "Roll Reverb", 0.15f, pct);
    add (id::rollTone, "Roll Tone", 0.45f, pct);
    add (id::rollSwing, "Roll Swing", 0.0f, pct);
    add (id::rollPan, "Roll Pan", 0.5f, [] (float v, int) { return std::abs (v - 0.5f) < 0.02f ? juce::String ("C") : (v < 0.5f ? "L " : "R ") + juce::String (juce::roundToInt (std::abs (v - 0.5f) * 200)); });
    add (id::rollDrive, "Roll Drive", 0.15f, pct);
    add (id::rollSound, "Roll Sound", 0.0f, [] (float v, int) { return juce::String (rollSoundName (soundOf (v))); });

    // PATTERNS
    add (id::patDensity, "Patterns Density", 0.5f, pct);
    add (id::patKit, "Patterns Tune", 0.5f, [] (float v, int) { const int s = juce::roundToInt ((v - 0.5f) * 12.0f); return (s > 0 ? "+" : "") + juce::String (s) + " st"; });
    add (id::patSwing, "Patterns Swing", 0.0f, pct);
    add (id::patFilter, "Patterns Filter", 0.5f, bip);
    add (id::patDecay, "Patterns Decay", 1.0f, pct);
    add (id::patDrive, "Patterns Drive", 0.2f, pct);
    add (id::patSend, "Patterns Reverb", 0.08f, pct);
    add (id::patAccent, "Patterns Accent", 0.4f, pct);

    // PAD
    add (id::padChord, "Pad Chord", 0.0f, [] (float v, int) { return "Degree " + juce::String (chordDegree (v) + 1); });
    add (id::padOpen, "Pad Open", 0.45f, pct);
    add (id::padTimbre, "Pad Timbre", 0.4f, pct);
    add (id::padAttack, "Pad Attack", 0.35f, [] (float v, int) { return juce::String (0.05f * std::pow (80.0f, v), 2) + " s"; });
    add (id::padRelease, "Pad Release", 0.45f, [] (float v, int) { return juce::String (0.1f * std::pow (60.0f, v), 2) + " s"; });
    add (id::padWidth, "Pad Width", 0.8f, pct);
    add (id::padSend, "Pad Reverb", 0.35f, pct);
    add (id::padArpRate, "Pad Arp Rate", 2.0f / 3.0f, [] (float v, int) { return juce::String (arpDiv (v).label); });
    add (id::padOctave, "Pad Octave", 0.5f, [] (float v, int) { const int o = octaveOf (v); return (o > 0 ? "+" : "") + juce::String (o); });
    onOff (id::padArp, "Pad Arp", false);
    onOff (id::padSus, "Pad Sus", false);

    // FX
    add (id::fxLevel, "FX Level", 0.8f, pct);
    add (id::fxSend, "FX Reverb", 0.45f, pct);
    add (id::fxTone, "FX Tone", 0.7f, pct);
    add (id::fxLength, "FX Riser Length", 1.0f / 3.0f, [] (float v, int) { return juce::String (riserBars (v)) + " bars"; });
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
        {{ mask ({ 0, 4, 8, 12 }), mask ({ 4, 12 }), mask ({ 0, 3, 7, 11, 15 }), mask ({ 2, 6, 10, 14 }), 0 }},
        {{ mask ({ 0, 4, 8, 12 }), mask ({ 12 }), 0xffff, mask ({ 2, 6, 10, 14 }), mask ({ 7, 15 }) }},
        {{ mask ({ 0, 10 }), mask ({ 4, 12 }), mask ({ 0, 2, 4, 6, 8, 10, 12, 14 }), mask ({ 14 }), mask ({ 7, 13 }) }},
        {{ mask ({ 0, 4, 8, 12 }), 0, mask ({ 2, 6, 10, 14 }), 0, mask ({ 3, 6, 11, 13 }) }}
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
    kit.prepare ((float) fs);
    for (int c = 0; c < 2; ++c) { noiseHP[c].set ((float) fs, 35.0f, 0.7f); noiseLP[c].set ((float) fs, 12000.0f, 0.7f); }
    controlQueue.reset (fs);
    queueReady.store (true);
    lastKey = lastDegree = -1; lastOct = 99;
}

void StationProcessor::sendControl (const juce::MidiMessage& m)
{
    if (! queueReady.load()) return;
    auto msg = m;
    msg.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    controlQueue.addMessageToQueue (msg);
}

void StationProcessor::setParam (const char* pid, float val)
{
    if (auto* prm = apvts.getParameter (pid))
        if (std::abs (prm->getValue() - val) > 1.0e-4f)
            prm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, val));
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
    return fr < tol ? bb : std::floor (bb) + 1.0;
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

int StationProcessor::activeRollDiv() const
{
    int best = -1, bestOrder = -1;
    for (int i = 0; i < 8; ++i)
        if (rollHeld[(size_t) i] && rollOrder[(size_t) i] > bestOrder) { best = i; bestOrder = rollOrder[(size_t) i]; }
    return best;
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
    const int prog = program.load();

    if (type == 2)
    {
        if (num == 24) { shiftHeld.store (val > 0); return; }
        if (num >= 1 && num <= 16) { pressPad (num - 1, val > 0); return; }
        if (val == 0) return;
        if (num >= 17 && num <= 20) { toggleParam (kOnIds[num - 17]); return; }
        switch (num)
        {
            case 21: barOffset = positiveMod (std::round (beat), 4.0); lastStepIdx = (juce::int64) std::floor ((beat - barOffset) * 4.0) - 1; break;
            case 22: toggleParam (id::quant); break;
            case 23:   // CAPTURE = HOLD: rolls latch instead of stopping on release
                holdMode.store (! holdMode.load());
                if (! holdMode.load()) for (int i = 0; i < 8; ++i) if (! padDown[(size_t) (4 + i)].load()) rollHeld[(size_t) i] = false;
                break;
            case 25: runAction (aSwell, 1); break;
            case 26: runAction (aDrop, 0); break;
            case 27: program.store ((prog + 1) % numPrograms); break;
            case 28: if (syncSource.load() == 0) { pressPad (-1, true); } break;   // tap tempo
            case 29:
                if (prog == progPatterns) selTrack.store ((selTrack.load() + 1) % numTracks);
                else if (prog == progRoll) setParam (id::rollPitch, 0.5f);
                break;
            default: break;
        }
        return;
    }

    const float val01 = (float) val / 127.0f;
    if (num >= 1 && num <= 4)      setParam (knobDef (prog, num - 1, shift).pid, val01);
    else if (num >= 5 && num <= 8) setParam (kLevelIds[num - 5], val01);
    else if (num == 9)
    {
        encoderTime.store (uiTime());
        const int dir = val < 64 ? 1 : -1;
        switch (prog)
        {
            case progBrume:
                setParam (id::brumeLength, v (id::brumeLength) + 0.02f * (float) dir); break;
            case progRoll:
                setParam (id::rollPitch, (std::round (rollSemis (v (id::rollPitch))) + (float) dir + 12.0f) / 24.0f); break;
            case progPatterns:
                if (shift) slot.store ((slot.load() + dir + numSlots) % numSlots);
                else       selTrack.store ((selTrack.load() + dir + numTracks) % numTracks);
                break;
            case progPad:
                if (auto* kp = apvts.getParameter (id::key))
                {
                    const int k = (int) v (id::key);
                    const int nk = shift ? (k + 12) % 24 : (k / 12) * 12 + (k % 12 + dir + 12) % 12;   // SHIFT: minor <-> major
                    kp->setValueNotifyingHost (kp->convertTo0to1 ((float) nk));
                }
                break;
            default: break;
        }
    }
}

void StationProcessor::pressPad (int pIdx, bool down)
{
    // tap tempo (FX pad or BROWSE)
    auto tap = [this]
    {
        if (syncSource.load() != 0) return;
        const double now = (double) sampleCounter / fs;
        for (int i = taps.size(); --i >= 0;) if (now - taps[i] > 2.0) taps.remove (i);
        taps.add (now);
        if (taps.size() >= 2)
            if (auto* bp = apvts.getParameter (id::bpm))
                bp->setValueNotifyingHost (bp->convertTo0to1 ((float) juce::jlimit (60.0, 200.0, 60.0 / ((taps.getLast() - taps[0]) / (taps.size() - 1)))));
        if (taps.size() > 6) taps.remove (0);
    };
    if (pIdx < 0) { tap(); return; }

    padDown[(size_t) pIdx].store (down);
    if (down) padHitTime[(size_t) pIdx].store (uiTime());
    const int prog = program.load();
    const bool shift = shiftHeld.load();
    const double bb = beat - barOffset;

    // SHIFT + top row (and pad 5) = choose the program
    if (shift && down && pIdx <= 4) { program.store (pIdx); return; }

    switch (prog)
    {
        case progBrume:
        {
            if (pIdx >= 8 && pIdx <= 10)   // momentary
            {
                if (pIdx == 8) freezeHeld = down;
                if (pIdx == 9) throwHeld = down;
                if (pIdx == 10) noiseHeld = down;
                return;
            }
            if (! down) return;
            if (pIdx < 4)
            {
                static const int div[] = { 1, 2, 4, 6 };   // 1/4 1/8 1/16 1/32
                setParam (id::brumeTremRate, (float) div[pIdx] / 6.0f);
                if (v (id::brumeTrem) < 0.05f) setParam (id::brumeTrem, 0.7f);
            }
            else if (pIdx < 8) { static const float len[] = { 1.0f, 3.0f, 6.0f, 12.0f }; setParam (id::brumeLength, lengthNorm (len[pIdx - 4])); }
            else if (pIdx == 11) setParam (id::brumeTrem, v (id::brumeTrem) > 0.05f ? 0.0f : 0.7f);
            else
            {
                static const float ch[] = { 0.0f, 0.4f, 0.75f, 1.0f }, len[] = { 1.2f, 2.5f, 5.0f, 11.0f };
                setParam (id::brumeChar, ch[pIdx - 12]); setParam (id::brumeLength, lengthNorm (len[pIdx - 12]));
            }
            return;
        }
        case progRoll:
        {
            if (pIdx < 4)
            {
                if (! down) return;
                setParam (id::rollSound, (float) pIdx / 3.0f);
                setParam (id::rollOn, 1.0f);
                triggerKit (pIdx == 3 ? 3 : pIdx, rollSemis (v (id::rollPitch)), 1.0f, 0.03f + 0.6f * v (id::rollDecay), v (id::rollPan), busRoll);
                return;
            }
            if (pIdx < 12)
            {
                const int d = pIdx - 4;
                if (down)
                {
                    setParam (id::rollOn, 1.0f);
                    if (holdMode.load() && rollHeld[(size_t) d]) { rollHeld[(size_t) d] = false; return; }   // HOLD: second press releases
                    rollHeld[(size_t) d] = true;
                    rollOrder[(size_t) d] = ++rollCounter;
                    // start on the grid; a press just after a grid point plays at once
                    const float rate = rollDiv (d).perBeat;
                    const double pos = bb * rate;
                    lastRollIdx = (juce::int64) std::floor (pos) - (pos - std::floor (pos) < 0.3 ? 1 : 0);
                    lastRollRate = rate;
                }
                else if (! holdMode.load()) rollHeld[(size_t) d] = false;
                return;
            }
            if (down) { static const int bars[] = { 2, 4, 8, 16 }; setParam (id::rollOn, 1.0f); schedule (nextBar(), aBuild, bars[pIdx - 12]); }
            return;
        }
        case progPatterns:
        {
            if (! down) return;
            auto& cell = pattern[(size_t) slot.load()][(size_t) selTrack.load()];
            if (shift)
            {
                if (pIdx >= 8 && pIdx < 12) slot.store (pIdx - 8);
                else if (pIdx == 12)        cell.store (0);
                else if (pIdx == 15)        loadPreset (slot.load());
                return;
            }
            cell.store (cell.load() ^ (1 << pIdx));
            return;
        }
        case progPad:
        {
            if (! down) return;
            if (pIdx < 8) { setParam (id::padChord, (float) pIdx / 7.0f); if (v (id::padOn) < 0.5f) setParam (id::padOn, 1.0f); }
            else if (pIdx == 8) toggleParam (id::padArp);
            else if (pIdx < 12) { static const int r[] = { 0, 2, 3 }; setParam (id::padArpRate, (float) r[pIdx - 9] / 3.0f); setParam (id::padArp, 1.0f); }
            else if (pIdx < 15) setParam (id::padOctave, (float) (pIdx - 12) / 2.0f);
            else toggleParam (id::padSus);
            return;
        }
        default:   // FX
        {
            if (! down) return;
            const bool q = v (id::quant) > 0.5f;
            auto shot = [&] (int type) { if (q) schedule (nextBeat(), aShot, type); else runAction (aShot, type); };
            switch (pIdx)
            {
                case 0: shot (0); break;
                case 1: shot (1); break;
                case 2: shot (2); break;
                case 3: runAction (aSwell, 1); break;
                case 4: runAction (aSwell, 2); break;
                case 5: runAction (aRiser, riserBars (v (id::fxLength))); break;
                case 6: runAction (aShot, 5); break;
                case 7: shot (6); break;
                case 8: runAction (aDrop, 0); break;
                case 9: runAction (aKill, 0); break;
                case 10: tap(); break;
                case 11: barOffset = positiveMod (std::round (beat), 4.0); lastStepIdx = (juce::int64) std::floor ((beat - barOffset) * 4.0) - 1; break;
                case 12: runAction (aDrop, 0); runAction (aShot, 0); break;
                case 13: runAction (aDrop, 0); runAction (aShot, 1); break;
                case 14: setParam (id::rollOn, 1.0f); schedule (nextBar(), aBuild, 8); break;
                default: setParam (id::rollOn, 1.0f); schedule (nextBar(), aBuild, 16); break;
            }
            return;
        }
    }
}

void StationProcessor::runAction (int action, int arg)
{
    const double bb = beat - barOffset;
    auto barsToSeconds = [this] (double beats) { return (float) (beats * 60.0 / std::max (40.0f, bpmUi.load())); };
    auto toNextBar = [&] (double extraBars) { double rem = (bb - positiveMod (bb, 4.0) + 4.0) - bb; if (rem < 1.0) rem += 4.0; return rem + 4.0 * extraBars; };
    switch (action)
    {
        case aStartPattern: running[2] = true; armed[2].store (false); lastStepIdx = (juce::int64) std::floor (bb * 4.0) - 1; break;
        case aStartPad:     running[3] = true; armed[3].store (false); break;
        case aBuild:
            buildActive = true; buildStart = bb; buildLen = 4.0 * arg;
            lastRollRate = 1.0f; lastRollIdx = (juce::int64) std::floor (bb) - 1;
            break;
        case aShot:
            shots[(size_t) nextShot].trigger ((float) fs, arg, arg == 5 ? barsToSeconds (2.0) : 1.0f);
            nextShot = (nextShot + 1) % (int) shots.size();
            killFade = 1.0f;
            break;
        case aSwell:
            shots[(size_t) nextShot].trigger ((float) fs, 3, barsToSeconds (toNextBar (arg == 2 ? 1.0 : 0.0)));
            nextShot = (nextShot + 1) % (int) shots.size();
            break;
        case aRiser:
            shots[(size_t) nextShot].trigger ((float) fs, 4, barsToSeconds (toNextBar ((double) arg - 1.0)));
            nextShot = (nextShot + 1) % (int) shots.size();
            break;
        case aDrop:
        case aKill:
            buildActive = false;
            for (auto& h : rollHeld) h = false;
            for (auto& pd : pending) pd.used = false;
            for (int c : { 0, 2, 3 }) { running[c] = false; armed[(size_t) c].store (false); setParam (kOnIds[c], 0.0f); prevOn[c] = false; }
            if (action == aKill)
            {
                for (auto& vv : rollVoices) vv.active = false;
                for (auto& vv : drumVoices) vv.active = false;
                for (auto& vv : percVoices) vv.active = false;
                for (auto& vv : shots) vv.active = false;
                for (auto& a : arps) a.t = 10.0f;
                reverb.clear();
                noiseGate = 0.0f; pad.gain = 0.0f;
            }
            break;
        default: break;
    }
}

void StationProcessor::triggerKit (int sound, float semis, float vel, float gate, float pan, int bus)
{
    if (bus == busRoll)
    {
        rollVoices[(size_t) nextRoll].trigger (kit.s[(size_t) sound], sound, (float) fs, semis, vel, gate, pan);
        nextRoll = (nextRoll + 1) % (int) rollVoices.size();
    }
    else
    {
        if (sound == 3)   // a closed hat chokes the open hat
            for (auto& d : drumVoices) if (d.active && d.sound == 4) d.gate = std::min (d.gate, d.t);
        drumVoices[(size_t) nextDrumV].trigger (kit.s[(size_t) sound], sound, (float) fs, semis, vel, gate, pan);
        nextDrumV = (nextDrumV + 1) % (int) drumVoices.size();
    }
}

void StationProcessor::stepTick (int step)
{
    playStep.store (step);
    const float d = v (id::patDensity);
    const float accent = v (id::patAccent);
    const float semis = std::round ((v (id::patKit) - 0.5f) * 12.0f);
    const float gate = 0.04f + 1.2f * v (id::patDecay) * v (id::patDecay);
    static const float addWeight[numTracks] = { 0.18f, 0.15f, 1.0f, 0.3f, 0.6f };
    static const int kitSound[numTracks] = { 2, 1, 3, 4, -1 };
    static const float pans[numTracks] = { 0.5f, 0.5f, 0.62f, 0.58f, 0.4f };
    const int s = slot.load();
    for (int t = 0; t < numTracks; ++t)
    {
        const bool on = (pattern[(size_t) s][(size_t) t].load() >> step) & 1;
        const float h = hashTable[t][step];
        float vel = 0.0f;
        if (on)
        {
            if (h >= std::max (0.0f, (0.5f - d) * 2.0f)) vel = (step % 4 == 0) ? 1.0f : 1.0f - 0.5f * accent;
        }
        else if (d > 0.5f && h < (d - 0.5f) * 2.0f * addWeight[t]) vel = 0.5f;
        if (vel <= 0.0f) continue;
        if (kitSound[t] >= 0) triggerKit (kitSound[t], semis, vel, gate, pans[t], busDrums);
        else
        {
            percVoices[(size_t) nextPerc].trigger ((float) fs, 4, vel, v (id::patKit));
            nextPerc = (nextPerc + 1) % (int) percVoices.size();
        }
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

    // ---- tempo: Traktor's MIDI clock, else the host, else internal ----
    int src = 0;
    double bpm = v (id::bpm), target = 0.0;
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
            { src = 2; bpm = *pos->getBpm(); target = *pos->getPpqPosition(); }
    }
    bpm = juce::jlimit (40.0, 250.0, bpm);
    if (src != 0)
    {
        const double diff = target - beat;
        if (std::abs (diff) > 0.5 || syncSource.load() != src)
        {
            beat = target;
            lastStepIdx = (juce::int64) std::floor ((beat - barOffset) * 4.0) - (running[2] ? 0 : 1);
            lastRollIdx = (juce::int64) std::floor ((beat - barOffset) * std::max (0.5f, lastRollRate));
        }
        else beat += diff * 0.2;
    }
    syncSource.store (src);
    bpmUi.store ((float) bpm);
    const double inc = bpm / 60.0 / fs;

    // ---- programs on/off (STOP buttons): BRUME and ROLL at once, PATTERNS on the bar, PAD on the beat ----
    const bool q = v (id::quant) > 0.5f;
    for (int c = 0; c < 4; ++c)
    {
        const bool on = v (kOnIds[c]) > 0.5f;
        if (on && ! prevOn[c])
        {
            if (c < 2) running[c] = true;
            else if (! q) runAction (c == 2 ? aStartPattern : aStartPad, 0);
            else { armed[(size_t) c].store (true); schedule (c == 2 ? nextBar() : nextBeat(), c == 2 ? aStartPattern : aStartPad, 0); }
        }
        else if (! on && prevOn[c])
        {
            running[c] = false; armed[(size_t) c].store (false);
            const int a = c == 2 ? aStartPattern : aStartPad;
            if (c >= 2) for (auto& pd : pending) if (pd.used && pd.action == a) pd.used = false;
        }
        prevOn[c] = on;
    }

    // ---- block-rate settings ----
    const int key = juce::jlimit (0, 23, (int) v (id::key));
    const int degree = chordDegree (v (id::padChord));
    const int oct = octaveOf (v (id::padOctave));
    const bool sus = v (id::padSus) > 0.5f;
    if (key != lastKey || degree != lastDegree || oct != lastOct || sus != lastSus)
    { pad.setChord (chordNotes (key, degree, oct, sus)); lastKey = key; lastDegree = degree; lastOct = oct; lastSus = sus; }
    kit.renderSnare (v (id::rollTone));

    reverb.setParameters (freezeHeld ? 600.0f : lengthSeconds (v (id::brumeLength)), v (id::brumeChar), v (id::brumeAir));
    for (int c = 0; c < 2; ++c)
    {
        brumeCut[c].set ((float) fs, 20.0f * std::pow (60.0f, v (id::brumeLowCut)), 0.7f);
        noiseLP[c].set ((float) fs, 1500.0f * std::pow (11.0f, v (id::brumeColor)), 0.7f);
        fxFilt[c].set ((float) fs, 600.0f * std::pow (30.0f, v (id::fxTone)), 0.7f);
    }
    // the build opens a high-pass on the roll as it climbs
    float rollFilterV = v (id::rollFilter);
    if (buildActive) rollFilterV = std::max (rollFilterV, 0.5f + 0.28f * (float) juce::jlimit (0.0, 1.0, ((beat - barOffset) - buildStart) / buildLen));
    bool rollHp = false, rollBypass = true, patHp = false, patBypass = true;
    setBipolar (rollFilt, (float) fs, rollFilterV, rollHp, rollBypass);
    setBipolar (patFilt, (float) fs, v (id::patFilter), patHp, patBypass);

    auto gainOf = [this] (const char* pid, float k) { const float x = v (pid); return x * x * k; };
    const float rollGain = gainOf (id::rollLevel, 1.5f) * (running[1] ? 1.0f : 0.0f);
    const float drumGain = gainOf (id::patLevel, 0.9f);
    const float padGain = gainOf (id::padLevel, 1.6f);
    const float noiseAmt = std::pow (v (id::brumeLevel), 0.8f) * 0.5f;
    const float fxGain = gainOf (id::fxLevel, 1.0f);
    const float rollDrive = 1.0f + 5.0f * v (id::rollDrive), drumDrive = 1.0f + 4.0f * v (id::patDrive);
    const float rollDriveNorm = 1.0f / std::tanh (rollDrive), drumDriveNorm = 1.0f / std::tanh (drumDrive);
    const float sRoll = v (id::rollSend), sDrum = v (id::patSend), sPad = v (id::padSend), sFx = v (id::fxSend);
    const float trem = v (id::brumeTrem), tremRate = tremDiv (v (id::brumeTremRate)).perBeat;
    const float rollSwing = v (id::rollSwing), patSwing = v (id::patSwing);
    const float rollSemisNow = rollSemis (v (id::rollPitch));
    const float rollGate = 0.03f + 0.6f * v (id::rollDecay) * v (id::rollDecay);
    const float rollPanV = v (id::rollPan);
    const int rollSnd = soundOf (v (id::rollSound));
    const float padOpenV = v (id::padOpen), padTimbreV = v (id::padTimbre), padWidthV = v (id::padWidth);
    const float attack = 0.05f * std::pow (80.0f, v (id::padAttack)), release = 0.1f * std::pow (60.0f, v (id::padRelease));
    const float padFade = 1.0f - std::exp (-1.0f / ((running[3] ? attack : release) * 0.3f * (float) fs));
    const bool arpOn = v (id::padArp) > 0.5f && running[3];
    const float arpRate = arpDiv (v (id::padArpRate)).perBeat;
    const float cNoiseOn = 1.0f - std::exp (-1.0f / (0.02f * (float) fs));
    const float cNoiseOff = 1.0f - std::exp (-1.0f / (lengthSeconds (v (id::brumeLength)) / 6.9f * (float) fs));
    const float smooth = 1.0f - std::exp (-1.0f / (0.03f * (float) fs));
    const float sendTarget = v (id::brumeSend) * (throwHeld ? 4.0f : 1.0f);
    const float masterTarget = std::pow (v (id::master), 2.0f) * 1.3f;
    const bool noiseOpen = running[0] || noiseHeld;

    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (1);
    auto* sL = sendBuf.getWritePointer (0);
    auto* sR = sendBuf.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        const double bb = beat - barOffset;
        for (auto& pd : pending)
            if (pd.used && bb >= pd.at) { pd.used = false; runAction (pd.action, pd.arg); }

        // ---- patterns: one step per 16th, odd steps late with swing ----
        if (running[2])
        {
            const double sp = bb * 4.0;
            auto idx = (juce::int64) std::floor (sp);
            if ((idx & 1) && sp - (double) idx < patSwing * 0.5) --idx;
            if (idx > lastStepIdx || idx < lastStepIdx - 2) { stepTick ((int) (((idx % 16) + 16) % 16)); lastStepIdx = idx; }
        }
        else lastStepIdx = (juce::int64) std::floor (bb * 4.0);

        // ---- roll: held pad division, or the automatic build ----
        float rate = 0.0f, extra = 0.0f, vel = 1.0f;
        if (buildActive)
        {
            const double f = (bb - buildStart) / buildLen;
            if (f >= 1.0) buildActive = false;   // ends exactly on the drop
            else
            {
                rate = f < 0.5 ? 1.0f : f < 0.75 ? 2.0f : f < 0.875 ? 4.0f : 8.0f;
                extra = (float) (12.0 * f);
                vel = (float) (0.5 + 0.5 * f);
            }
        }
        else
        {
            const int d = activeRollDiv();
            if (d >= 0) rate = rollDiv (d).perBeat;
        }
        if (rate > 0.0f)
        {
            if (rate != lastRollRate) { lastRollIdx = (juce::int64) std::floor (bb * rate); lastRollRate = rate; }
            const double rp = bb * rate;
            auto ri = (juce::int64) std::floor (rp);
            if ((ri & 1) && rp - (double) ri < rollSwing * 0.5) --ri;
            if (ri > lastRollIdx)
            {
                triggerKit (rollSnd, rollSemisNow + extra, vel, rollGate, rollPanV, busRoll);
                lastRollIdx = ri;
            }
        }

        // ---- arpeggio over the chord ----
        if (arpOn)
        {
            const auto ai = (juce::int64) std::floor (bb * arpRate);
            if (ai != lastArpIdx)
            {
                lastArpIdx = ai;
                const int voice = 1 + (arpStep % 4 == 3 ? 0 : arpStep % 4);
                const float hz = pad.targetHz[(size_t) std::min (3, voice)] * (arpStep % 4 == 3 ? 2.0f : 1.0f);
                arps[(size_t) nextArp].trigger ((float) fs, hz, 0.8f);
                nextArp = (nextArp + 1) % (int) arps.size();
                ++arpStep;
            }
        }

        // ---- render buses ----
        float rl = 0.0f, rr = 0.0f;
        for (auto& vv : rollVoices) { if (! vv.active) continue; const float y = vv.tick(); rl += y * (1.0f - vv.pan) * 1.414f; rr += y * vv.pan * 1.414f; }
        if (! rollBypass) { rl = rollHp ? rollFilt[0].tick (rl).hp : rollFilt[0].tick (rl).lp; rr = rollHp ? rollFilt[1].tick (rr).hp : rollFilt[1].tick (rr).lp; }
        rl = std::tanh (rl * rollDrive) * rollDriveNorm * rollGain;
        rr = std::tanh (rr * rollDrive) * rollDriveNorm * rollGain;

        float dl = 0.0f, dr = 0.0f;
        for (auto& vv : drumVoices) { if (! vv.active) continue; const float y = vv.tick(); dl += y * (1.0f - vv.pan) * 1.414f; dr += y * vv.pan * 1.414f; }
        for (auto& pv : percVoices) { if (! pv.active) continue; const float y = pv.tick(); dl += y * 0.85f; dr += y * 0.6f; }
        if (! patBypass) { dl = patHp ? patFilt[0].tick (dl).hp : patFilt[0].tick (dl).lp; dr = patHp ? patFilt[1].tick (dr).hp : patFilt[1].tick (dr).lp; }
        dl = std::tanh (dl * drumDrive) * drumDriveNorm * drumGain;
        dr = std::tanh (dr * drumDrive) * drumDriveNorm * drumGain;

        float fx = 0.0f;
        for (auto& sh : shots) if (sh.active) fx += sh.tick();
        const float fxl = fxFilt[0].tick (fx).lp * fxGain, fxr = fxFilt[1].tick (fx).lp * fxGain;

        float pl, pr;
        pad.tick (running[3] ? (arpOn ? 0.45f : 1.0f) : 0.0f, padFade, padOpenV, padTimbreV, padWidthV, pl, pr);
        float arp = 0.0f;
        for (auto& a : arps) arp += a.tick (padOpenV);
        pl = (pl + arp * 0.09f) * padGain; pr = (pr + arp * 0.09f) * padGain;

        // BRUME noise with synced tremolo
        noiseGate += ((noiseOpen ? 1.0f : 0.0f) - noiseGate) * (noiseOpen ? cNoiseOn : cNoiseOff);
        float nl = 0.0f, nr = 0.0f;
        if (noiseGate > 1.0e-5f)
        {
            const float common = noiseRng.next();
            const float a = (common * 0.71f + noiseRng.next() * 0.71f) * 0.5f;
            const float b = (common * 0.71f + noiseRng.next() * 0.71f) * 0.5f;
            const float tr = 1.0f - trem * (0.5f - 0.5f * std::cos (station::kTwoPi * (float) positiveMod (bb * tremRate, 1.0)));
            const float g = noiseAmt * noiseGate * tr * (noiseHeld && ! running[0] ? 1.4f : 1.0f);
            nl = noiseLP[0].tick (noiseHP[0].tick (a).hp).lp * g;
            nr = noiseLP[1].tick (noiseHP[1].tick (b).hp).lp * g;
        }

        sendSm += (sendTarget - sendSm) * smooth;
        const float sendAmt = freezeHeld ? 0.0f : sendSm * 2.0f;
        sL[i] = (rl * sRoll + dl * sDrum + pl * sPad + fxl * sFx) * sendAmt + (freezeHeld ? 0.0f : nl * 0.6f);
        sR[i] = (rr * sRoll + dr * sDrum + pr * sPad + fxr * sFx) * sendAmt + (freezeHeld ? 0.0f : nr * 0.6f);
        outL[i] = rl + dl + fxl + pl + nl * 0.85f;
        outR[i] = rr + dr + fxr + pr + nr * 0.85f;

        beat += inc;
    }

    // ---- Brume reverb on the send bus (low cut on Brume's own sound), then master ----
    reverb.process (sL, sR, n);
    for (int i = 0; i < n; ++i)
    {
        masterSm += (masterTarget - masterSm) * smooth;
        const float wl = brumeCut[0].tick (sL[i] * 1.3f).hp, wr = brumeCut[1].tick (sR[i] * 1.3f).hp;
        outL[i] = std::tanh ((outL[i] + wl) * masterSm);
        outR[i] = std::tanh ((outR[i] + wr) * masterSm);
    }

    sampleCounter += n;
    barBeatUi.store ((float) positiveMod (beat - barOffset, 4.0));
    buildLeftUi.store (buildActive ? (float) (buildStart + buildLen - (beat - barOffset)) : -1.0f);
    if (! running[2]) playStep.store (-1);
}

//==============================================================================
float StationProcessor::padFlash (int pIdx) const
{
    const double dt = uiTime() - padHitTime[(size_t) pIdx].load();
    return dt < 0.0 || dt > 0.25 ? 0.0f : (float) (1.0 - dt / 0.25);
}

juce::String StationProcessor::getPadLabel (int pIdx) const
{
    const int prog = program.load();
    if (shiftHeld.load())
    {
        if (pIdx <= 4) return programName (pIdx);
        if (prog == progPatterns)
        {
            if (pIdx >= 8 && pIdx < 12) return "SLOT " + juce::String (pIdx - 7);
            if (pIdx == 12) return "CLEAR";
            if (pIdx == 15) return "RESET";
        }
        return {};
    }
    switch (prog)
    {
        case progBrume:
        {
            static const char* n[] = { "TREM 1/4", "TREM 1/8", "TREM 1/16", "TREM 1/32", "1 s", "3 s", "6 s", "12 s",
                                       "FREEZE", "THROW", "NOISE", "TREM ON", "SMALL", "PLATE", "HALL", "VAST" };
            return n[pIdx];
        }
        case progRoll:
        {
            if (pIdx < 4) return rollSoundName (pIdx);
            if (pIdx < 12) return juce::String ("ROLL ") + rollDiv (pIdx - 4).label;
            static const char* b[] = { "BUILD 2", "BUILD 4", "BUILD 8", "BUILD 16" };
            return b[pIdx - 12];
        }
        case progPatterns: return juce::String (pIdx + 1);
        case progPad:
        {
            const bool major = keyIsMajor ((int) v (id::key));
            if (pIdx < 7) return degreeName (pIdx, major);
            if (pIdx == 7) return juce::String (degreeName (0, major)) + " +8";
            static const char* n[] = { "ARP", "ARP 1/8", "ARP 1/16", "ARP 1/16T", "OCT -1", "OCT 0", "OCT +1", "SUS" };
            return n[pIdx - 8];
        }
        default:
        {
            static const char* n[] = { "IMPACT", "CRASH", "CLAP", "SWELL", "SWELL 2", "RISER", "DOWN", "SUB DROP",
                                       "DROP", "KILL", "TAP", "BAR 1", "DROP+IMP", "DROP+CRASH", "BUILD 8", "BUILD 16" };
            return n[pIdx];
        }
    }
}

juce::String StationProcessor::getDisplayText() const
{
    const float left = buildLeftUi.load();
    if (left >= 0.0f) return juce::String ((int) std::ceil (left));
    const int prog = program.load();
    if (uiTime() - encoderTime.load() < 1.5)
    {
        if (prog == progPad) { const int k = (int) v (id::key); return juce::String (k % 12 + 1) + (k < 12 ? "A" : "B"); }
        if (prog == progPatterns) return "t" + juce::String (selTrack.load() + 1);
        if (prog == progRoll) return juce::String (juce::roundToInt (rollSemis (v (id::rollPitch))));
    }
    return "P" + juce::String (prog + 1);
}

juce::String StationProcessor::getLastEvent() const
{
    const int code = lastEventCode.load();
    if (code < 0) return {};
    return F1Reader::controlName (code >> 8, code & 0xff);
}

void StationProcessor::timerCallback() { buildLeds(); }

void StationProcessor::buildLeds()
{
    auto scale = [] (juce::uint32 c, float k) { return ((juce::uint32) std::min (255.0f, ((c >> 16) & 255) * k) << 16) | ((juce::uint32) std::min (255.0f, ((c >> 8) & 255) * k) << 8) | (juce::uint32) std::min (255.0f, (c & 255) * k); };
    const float bb = barBeatUi.load();
    const bool onBeat = (bb - std::floor (bb)) < 0.2f;
    const bool fastBlink = std::fmod (uiTime() * 6.0, 1.0) < 0.5;
    const int prog = program.load();
    const bool shift = shiftHeld.load();
    static const juce::uint32 trackCol[numTracks] = { 0xff3030, 0xff8a1f, 0xffd830, 0x30d8ff, 0xb050ff };

    for (int pI = 0; pI < 16; ++pI)
    {
        juce::uint32 c = 0;
        const int row = pI / 4, col = pI % 4;
        if (shift)
        {
            if (pI <= 4) c = pI == prog ? 0xffffff : scale (programColour (pI), 0.5f);
            else if (prog == progPatterns && pI >= 8 && pI < 12) c = (pI - 8) == slot.load() ? 0xffffff : 0x303030;
            else if (prog == progPatterns && (pI == 12 || pI == 15)) c = pI == 12 ? 0xa00000 : 0xa05000;
        }
        else switch (prog)
        {
            case progBrume:
                if (row == 0) { const int div[] = { 1, 2, 4, 6 }; c = std::lround (v (id::brumeTremRate) * 6.0f) == div[col] && v (id::brumeTrem) > 0.05f ? 0x60e0ff : 0x0a2a38; }
                else if (row == 1) { const float len[] = { 1, 3, 6, 12 }; c = std::abs (lengthSeconds (v (id::brumeLength)) - len[col]) < 0.3f * len[col] ? 0x4080ff : 0x0a1838; }
                else if (row == 2) { c = col == 3 ? (v (id::brumeTrem) > 0.05f ? 0x60e0ff : 0x0a2a38) : (padDown[(size_t) pI].load() ? 0xe0b0ff : 0x301848); }
                else c = 0x0a3030;
                break;
            case progRoll:
                if (row == 0) c = soundOf (v (id::rollSound)) == col ? 0xff8a1f : 0x301800;
                else if (row < 3) c = rollHeld[(size_t) (pI - 4)] ? (onBeat ? 0xffd060 : 0xffa020) : 0x281400;
                else { const float bars[] = { 2, 4, 8, 16 }; c = 0x380808; if (buildLeftUi.load() >= 0.0f && std::abs (buildLen - bars[col] * 4.0f) < 0.1f) c = onBeat ? 0xff3020 : 0x801010; }
                break;
            case progPatterns:
            {
                const int t = selTrack.load();
                const bool on = stepIsOn (slot.load(), t, pI);
                c = on ? trackCol[t] : scale (trackCol[t], 0.06f);
                if (pI == playStep.load()) c = on ? 0xffffff : 0x505050;
                break;
            }
            case progPad:
                if (pI < 8) c = chordDegree (v (id::padChord)) == pI ? 0x40a0ff : 0x081830;
                else if (pI == 8) c = v (id::padArp) > 0.5f ? 0x30ff60 : 0x082010;
                else if (pI < 12) { const int r[] = { 0, 2, 3 }; c = v (id::padArp) > 0.5f && std::lround (v (id::padArpRate) * 3.0f) == r[pI - 9] ? 0x30ff60 : 0x082010; }
                else if (pI < 15) c = octaveOf (v (id::padOctave)) == pI - 13 ? 0xb050ff : 0x200830;
                else c = v (id::padSus) > 0.5f ? 0xb050ff : 0x200830;
                break;
            default:
                c = row == 0 ? 0x383838 : row == 1 ? 0x301040 : row == 2 ? (col == 0 ? 0xff1010 : col == 1 ? 0x400000 : col == 2 ? (onBeat ? 0x30ff40 : 0x083010) : 0x202020)
                  : (col < 2 ? 0x601010 : 0x380808);
                break;
        }
        const float fl = padFlash (pI);
        if (fl > 0.0f) c = scale (c, 1.0f + 3.0f * fl) | (fl > 0.5f ? 0x404040u : 0u);
        padRgb[(size_t) pI].store (c);
    }

    if (f1Reader.getStatus() != F1Reader::connected) return;

    // ---- F1 light report (layout from the Mixxx mapping) ----
    juce::uint8 rep[F1Reader::ledReportSize] {};
    rep[0] = 0x80;
    // segment bit order in the report: g, c, b, a, f, e, d
    auto glyph = [] (juce::juce_wchar ch) -> juce::uint8
    {
        static const juce::uint8 digits[10] = { 0x7e, 0x06, 0x6d, 0x4f, 0x17, 0x5b, 0x7b, 0x0e, 0x7f, 0x5f };
        if (ch >= '0' && ch <= '9') return digits[ch - '0'];
        switch (ch)
        {
            case 'P': return 0x3d; case 'A': return 0x3f; case 'B': return 0x73; case 't': return 0x71;
            case '-': return 0x01; default: return 0;
        }
    };
    auto putChar = [&] (int base, juce::juce_wchar ch, bool dp)
    {
        rep[base] = dp ? 0x7f : 0;
        const auto g = glyph (ch);
        for (int i = 0; i < 7; ++i) rep[base + 1 + i] = (g >> i) & 1 ? 0x7f : 0;
    };
    {
        auto text = getDisplayText();
        bool dp = false;
        if (text.endsWithChar ('B') && text.length() > 1 && juce::CharacterFunctions::isDigit (text[0])) { dp = true; text = text.dropLastCharacters (1); }
        else if (text.endsWithChar ('A') && text.length() > 1 && juce::CharacterFunctions::isDigit (text[0])) text = text.dropLastCharacters (1);
        if (text.length() > 2) text = text.getLastCharacters (2);
        const juce::juce_wchar l = text.length() == 2 ? text[0] : 0, r = text.length() >= 1 ? text[text.length() - 1] : 0;
        putChar (1, r, dp);    // right digit (dot = major key)
        putChar (9, l, false); // left digit
    }
    rep[17] = onBeat ? 0x7f : 0x10;                                 // browse (tap)
    rep[18] = 0x10;                                                 // size (next program)
    rep[19] = 0x30;                                                 // type (drop)
    rep[20] = 0x10;                                                 // reverse (swell)
    rep[21] = shift ? 0x7f : 0x08;                                  // shift
    rep[22] = holdMode.load() ? 0x7f : 0x08;                        // capture = hold
    rep[23] = v (id::quant) > 0.5f ? 0x7f : 0x08;                   // quant
    rep[24] = bb < 0.25f ? 0x7f : 0x08;                             // sync: downbeat
    for (int pI = 0; pI < 16; ++pI)
    {
        const auto c = padRgb[(size_t) pI].load();
        rep[25 + 3 * pI]     = (juce::uint8) ((c & 255) >> 1);
        rep[25 + 3 * pI + 1] = (juce::uint8) (((c >> 16) & 255) >> 1);
        rep[25 + 3 * pI + 2] = (juce::uint8) (((c >> 8) & 255) >> 1);
    }
    for (int c = 0; c < 4; ++c)
    {
        const bool on = v (kOnIds[c]) > 0.5f;
        const juce::uint8 val = armed[(size_t) c].load() ? (fastBlink ? 0x7f : 0x00) : on ? 0x7f : 0x0a;
        const int off = 79 - 2 * c;
        rep[off] = rep[off + 1] = val;
    }
    f1Reader.setLeds (rep);
}

//==============================================================================
void StationProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::ValueTree pats ("PATTERNS");
    pats.setProperty ("slot", slot.load(), nullptr);
    pats.setProperty ("program", program.load(), nullptr);
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
        program.store (juce::jlimit (0, numPrograms - 1, (int) pats.getProperty ("program", (int) progRoll)));
        for (int s = 0; s < numSlots; ++s)
            for (int t = 0; t < numTracks; ++t)
            {
                const auto k = "s" + juce::String (s) + "t" + juce::String (t);
                if (pats.hasProperty (k)) pattern[(size_t) s][(size_t) t].store ((int) pats.getProperty (k));
            }
        state.removeChild (pats, nullptr);
    }
    apvts.replaceState (state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StationProcessor(); }
