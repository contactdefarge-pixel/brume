#include "PluginEditor.h"
#include "BinaryData.h"

using namespace brume::ui;

namespace
{
    constexpr int kW = 520, kH = 720;

    juce::Font silkFont (float h, float kerning = 0.12f)
    {
        juce::Font f (juce::FontOptions (h, juce::Font::bold));
        f.setExtraKerningFactor (kerning);
        return f;
    }

    // logo typeface (Michroma, embedded)
    juce::Font logoFont (float h, float kerning)
    {
        static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (BinaryData::MichromaRegular_ttf,
                                                                                  BinaryData::MichromaRegular_ttfSize);
        juce::Font f (juce::FontOptions (tf).withHeight (h));
        f.setExtraKerningFactor (kerning);
        return f;
    }

    void drawScrew (juce::Graphics& g, juce::Point<float> c, float r)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8d8d2), c.x - r * 0.4f, c.y - r * 0.4f,
                                                 juce::Colour (0xff6a6d6c), c.x + r, c.y + r, true));
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (juce::Colour (0xff3b3d3c));
        juce::Path p;
        p.addRectangle (-r * 0.6f, -r * 0.09f, r * 1.2f, r * 0.18f);
        g.fillPath (p, juce::AffineTransform::rotation (0.6f).translated (c));
    }

    void drawLed (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, bool on)
    {
        if (on)
        {
            g.setGradientFill (juce::ColourGradient (col.withAlpha (0.55f), c.x, c.y, col.withAlpha (0.0f), c.x + r * 3.2f, c.y, true));
            g.fillEllipse (c.x - r * 3.2f, c.y - r * 3.2f, r * 6.4f, r * 6.4f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white, c.x - r * 0.3f, c.y - r * 0.3f, col, c.x + r, c.y + r, true));
        }
        else
        {
            g.setColour (ledOff);
        }
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawEllipse (c.x - r, c.y - r, r * 2, r * 2, 1.0f);
    }
}

//==============================================================================
void BrumeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider&)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float size = std::min (area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float rOuter = size * 0.5f;
    const float rKnob = rOuter * 0.74f;

    // scale ticks
    g.setColour (silk.withAlpha (0.75f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
        const auto p1 = c.getPointOnCircumference (rOuter * 0.86f, a);
        const auto p2 = c.getPointOnCircumference (rOuter * 0.98f, a);
        g.drawLine ({ p1, p2 }, i == 0 || i == 10 || i == 5 ? 1.6f : 1.0f);
    }

    // shadow on the enclosure
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillEllipse (c.x - rKnob * 1.02f, c.y - rKnob + size * 0.06f, rKnob * 2.04f, rKnob * 2.04f);

    // black skirt with a soft bevel
    g.setGradientFill (juce::ColourGradient (knobHi, c.x - rKnob * 0.5f, c.y - rKnob * 0.8f, knob, c.x + rKnob * 0.3f, c.y + rKnob, false));
    g.fillEllipse (c.x - rKnob, c.y - rKnob, rKnob * 2, rKnob * 2);

    // glossy cap
    const float rCap = rKnob * 0.80f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2e31), c.x - rCap * 0.35f, c.y - rCap * 0.55f, knob, c.x + rCap * 0.4f, c.y + rCap * 0.7f, true));
    g.fillEllipse (c.x - rCap, c.y - rCap, rCap * 2, rCap * 2);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawEllipse (c.x - rCap, c.y - rCap, rCap * 2, rCap * 2, 1.0f);
    {
        juce::Path shine;   // specular arc, top-left
        shine.addCentredArc (c.x, c.y, rCap * 0.82f, rCap * 0.82f, 0.0f, -2.2f, -0.6f, true);
        g.setColour (juce::Colours::white.withAlpha (0.13f));
        g.strokePath (shine, juce::PathStrokeType (rCap * 0.12f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // white pointer
    const float angle = startAngle + pos * (endAngle - startAngle);
    juce::Path ptr;
    const float pw = std::max (3.0f, rKnob * 0.19f);
    ptr.addRoundedRectangle (-pw * 0.5f, -rCap * 0.95f, pw, rCap * 0.72f, pw * 0.5f);
    g.setColour (pointer);
    g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c));
}

void BrumeLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    // vertical mini toggle: MIX on top, WET at the bottom
    const auto r = b.getLocalBounds().toFloat();
    const float slotW = 18.0f, slotH = 36.0f;
    const auto slot = juce::Rectangle<float> (slotW, slotH).withCentre (r.getCentre());
    g.setColour (juce::Colour (0xff1b1d1c));
    g.fillRoundedRectangle (slot, 5.0f);
    const bool wet = b.getToggleState();
    auto lever = juce::Rectangle<float> (slotW - 6.0f, 15.0f).withCentre ({ slot.getCentreX(), wet ? slot.getBottom() - 10.5f : slot.getY() + 10.5f });
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2f2ee), lever.getX(), lever.getY(), juce::Colour (0xff6c6f70), lever.getX(), lever.getBottom(), false));
    g.fillRoundedRectangle (lever, 3.5f);

    g.setFont (silkFont (10.5f));
    g.setColour (silk.withAlpha (wet ? 0.45f : 1.0f));
    g.drawText ("MIX", juce::Rectangle<float> (r.getX(), slot.getY() - 18.0f, r.getWidth(), 14.0f), juce::Justification::centred);
    g.setColour (silk.withAlpha (wet ? 1.0f : 0.45f));
    g.drawText ("WET", juce::Rectangle<float> (r.getX(), slot.getBottom() + 4.0f, r.getWidth(), 14.0f), juce::Justification::centred);
}

void Footswitch::paintButton (juce::Graphics& g, bool, bool down)
{
    // black arcade-style push button: bezel ring + slightly domed cap
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    const auto c = r.getCentre();
    const float R = std::min (r.getWidth(), r.getHeight()) * 0.5f;

    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.fillEllipse (c.x - R, c.y - R + 5.0f, R * 2, R * 2);

    // bezel
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d4044), c.x - R * 0.6f, c.y - R * 0.8f,
                                             juce::Colour (0xff050506), c.x + R * 0.5f, c.y + R, false));
    g.fillEllipse (c.x - R, c.y - R, R * 2, R * 2);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (c.x - R + 1.0f, c.y - R + 1.0f, R * 2 - 2.0f, R * 2 - 2.0f, 1.2f);

    // gap
    const float r2 = R * 0.80f;
    g.setColour (juce::Colour (0xff020203));
    g.fillEllipse (c.x - r2, c.y - r2, r2 * 2, r2 * 2);

    // cap (sinks a little when pressed)
    const float r3 = R * (down ? 0.72f : 0.75f);
    const float dy = down ? 1.5f : 0.0f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (down ? 0xff1c1e20 : 0xff26292c), c.x - r3 * 0.4f, c.y - r3 * 0.6f + dy,
                                             juce::Colour (0xff070708), c.x + r3 * 0.5f, c.y + r3 * 0.8f + dy, true));
    g.fillEllipse (c.x - r3, c.y - r3 + dy, r3 * 2, r3 * 2);
    juce::Path shine;
    shine.addCentredArc (c.x, c.y + dy, r3 * 0.80f, r3 * 0.80f, 0.0f, -2.4f, -0.5f, true);
    g.setColour (juce::Colours::white.withAlpha (down ? 0.10f : 0.18f));
    g.strokePath (shine, juce::PathStrokeType (r3 * 0.08f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
BrumeEditor::BrumeEditor (BrumeProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    const std::array<std::pair<const char*, const char*>, 8> defs {{
        { brume::id::reverb, "REVERB" }, { brume::id::character, "CHARACTER" }, { brume::id::length, "LENGTH" }, { brume::id::air, "AIR" },
        { brume::id::noise, "NOISE" }, { brume::id::tremolo, "TREMOLO" }, { brume::id::tempo, "TEMPO" }, { brume::id::filter, "FILTER" } }};

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.id = defs[i].first;
        k.label = defs[i].second;
        k.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.slider.setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
        k.slider.setMouseDragSensitivity (220);
        k.slider.setVelocityModeParameters (0.6, 1, 0.0, false, juce::ModifierKeys::shiftModifier);
        k.slider.setPopupDisplayEnabled (false, false, nullptr);
        addAndMakeVisible (k.slider);
        k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, k.id, k.slider);
        if (auto* prm = proc.apvts.getParameter (k.id))
            k.slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        const auto id = k.id;
        k.slider.onDragStart = [this, id] { showParam (id); };
        k.slider.onValueChange = [this, id] { showParam (id); };
    }

    mixToggle.setClickingTogglesState (true);
    addAndMakeVisible (mixToggle);
    mixAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, brume::id::wet, mixToggle);
    mixToggle.onClick = [this] { showParam (brume::id::wet); };

    onSwitch.setClickingTogglesState (true);
    addAndMakeVisible (onSwitch);
    onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, brume::id::on, onSwitch);
    onSwitch.onClick = [this] { showParam (brume::id::on); };

    tapSwitch.setTriggeredOnMouseDown (true);
    tapSwitch.onClick = [this] { proc.tap(); showParam (brume::id::tempo); };
    addAndMakeVisible (tapSwitch);

    tapSmall.setButtonText ("");
    tapSmall.setAlpha (0.01f);   // invisible hit area over the small TAP label + LED
    tapSmall.setTriggeredOnMouseDown (true);
    tapSmall.onClick = [this] { proc.tap(); showParam (brume::id::tempo); };
    addAndMakeVisible (tapSmall);

    midiButton.setClickingTogglesState (true);
    midiButton.setColour (juce::TextButton::buttonColourId, juce::Colours::black.withAlpha (0.22f));
    midiButton.setColour (juce::TextButton::buttonOnColourId, amber);
    midiButton.setColour (juce::TextButton::textColourOffId, ink);
    midiButton.setColour (juce::TextButton::textColourOnId, desk);
    midiButton.setColour (juce::ComboBox::outlineColourId, line);
    midiButton.setTooltip ("MIDI learn");
    midiButton.onClick = [this] { setLearnMode (midiButton.getToggleState()); };
    addAndMakeVisible (midiButton);

    recButton.setClickingTogglesState (true);
    recButton.setColour (juce::TextButton::buttonColourId, juce::Colours::black.withAlpha (0.22f));
    recButton.setColour (juce::TextButton::buttonOnColourId, ledRed);
    recButton.setColour (juce::TextButton::textColourOffId, ink);
    recButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    recButton.setColour (juce::ComboBox::outlineColourId, line);
    recButton.onClick = [this]
    {
        if (recButton.getToggleState())
        {
            if (! proc.startRecording())
            {
                recButton.setToggleState (false, juce::dontSendNotification);
                readoutName = "REC"; readoutValue = "Could not create the file in Music/Brume";
            }
        }
        else
        {
            proc.stopRecording();
            readoutName = "SAVED";
            readoutValue = proc.getLastRecording().getFileName();
            proc.getLastRecording().revealToUser();
        }
        lastParam.clear();
        repaint (0, 650, kW, 70);
    };
    addAndMakeVisible (recButton);
    addChildComponent (overlay);
    if (Updater::isSupported()) addChildComponent (updateBanner);

    setSize (kW, kH);
    timerCallback();
    startTimerHz (30);
}

BrumeEditor::~BrumeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

juce::Rectangle<float> BrumeEditor::pedalBounds() const { return { 20.0f, 44.0f, 480.0f, 600.0f }; }

void BrumeEditor::showParam (const juce::String& paramId)
{
    if (auto* prm = proc.apvts.getParameter (paramId))
    {
        lastParam = paramId;
        readoutName = prm->getName (32).toUpperCase();
        readoutValue = paramId == brume::id::tempo ? tempoText() : prm->getCurrentValueAsText();
        if (paramId == brume::id::on && prm->getValue() < 0.5f)
            readoutValue = "Off - the tail fades over " + proc.apvts.getParameter (brume::id::length)->getCurrentValueAsText();
        repaint (0, 650, kW, 70);
    }
}

void BrumeEditor::timerCallback()
{
    const float rate = brume::tremoloRateHz (proc.apvts.getRawParameterValue (brume::id::tempo)->load());
    const bool tapOn = rate > 12.0f || proc.getTremoloPhase() < 0.3f;
    const bool on = proc.apvts.getRawParameterValue (brume::id::on)->load() > 0.5f;
    if (tapOn != tapLedOn || on != onLed)
    {
        tapLedOn = tapOn; onLed = on;
        repaint();
    }
    if (lastParam.isNotEmpty() && lastParam != brume::id::on)
        if (auto* prm = proc.apvts.getParameter (lastParam))
        {
            const auto txt = lastParam == brume::id::tempo ? tempoText() : prm->getCurrentValueAsText();
            if (txt != readoutValue) { readoutValue = txt; repaint (0, 650, kW, 70); }
        }

    // F1 read directly over USB: tell when it connects or is locked by another app
    if (const int f1 = (int) proc.getF1Status(); f1 != shownF1)
    {
        const bool first = shownF1 < 0;
        shownF1 = f1;
        if (f1 == F1Reader::connected || (f1 == F1Reader::busy && ! first) || f1 == F1Reader::busy)
        {
            readoutName = "F1";
            readoutValue = f1 == F1Reader::connected ? "read directly over USB - MIDI learn ready"
                                                     : "found but locked by another app (Traktor?)";
            lastParam.clear();
            repaint (0, 650, kW, 70);
        }
    }

    // MIDI monitor: show every control message that reaches Brume
    if (const int act = proc.getMidiActivity(); act != shownMidiActivity)
    {
        shownMidiActivity = act;
        if (! proc.isRecording())
        {
            readoutName = "MIDI IN";
            readoutValue = BrumeProcessor::describeMapping (proc.getLastMidiCode()) + "  =  " + juce::String (proc.getLastMidiValue());
            lastParam.clear();
            repaint (0, 650, kW, 70);
        }
    }

    if (proc.isRecording())
    {
        const int secs = (int) proc.getRecordedSeconds();
        const auto txt = juce::String::formatted ("%d:%02d  -  Music/Brume", secs / 60, secs % 60);
        if (txt != readoutValue || readoutName != "REC")
        {
            readoutName = "REC"; readoutValue = txt; lastParam.clear();
            repaint (0, 650, kW, 70);
        }
    }

    const int sync = proc.getSyncSource();
    const int bpm10 = juce::roundToInt (proc.getSyncBpm() * 10.0f);
    if (sync != shownSync || (sync != 0 && std::abs (bpm10 - shownBpm10) >= 2))
    {
        shownSync = sync; shownBpm10 = bpm10;
        repaint (knobs[6].slider.getBounds().expanded (110, 40));
    }

    if (learnMode)
    {
        const int learned = proc.popLearnedTarget();
        if (learned >= 0)
        {
            readoutName = BrumeProcessor::midiTargetName (learned);
            readoutValue = "linked to " + BrumeProcessor::describeMapping (proc.getMidiMapping (learned));
            repaint (0, 650, kW, 70);
        }
        ++overlay.blink;
        overlay.repaint();
    }
}

juce::String BrumeEditor::tempoText() const
{
    const float v = proc.apvts.getRawParameterValue (brume::id::tempo)->load();
    const int sync = proc.getSyncSource();
    if (sync == 0)
        return juce::String (brume::tremoloRateHz (v), 2) + " Hz (free)";
    return juce::String (brume::division (v).label) + "  -  synced to " + juce::String (proc.getSyncBpm(), 1)
         + " BPM (" + (sync == 1 ? "MIDI clock" : "host") + ")";
}

juce::Rectangle<int> BrumeEditor::targetBounds (int t) const
{
    if (t >= 0 && t < 8) return knobs[(size_t) t].slider.getBounds();
    if (t == 8) return mixToggle.getBounds();
    if (t == 9) return onSwitch.getBounds();
    if (t == 10) return tapSwitch.getBounds();
    return {};
}

void BrumeEditor::setLearnMode (bool shouldLearn)
{
    learnMode = shouldLearn;
    midiButton.setToggleState (shouldLearn, juce::dontSendNotification);
    overlay.setVisible (shouldLearn);
    proc.setLearnTarget (-1);
    lastParam.clear();
    readoutName = shouldLearn ? "MIDI LEARN" : "BRUME";
    readoutValue = shouldLearn ? "Click a control, then move it on your controller" : "MIDI learn off";
    repaint();
}

//==============================================================================
void LearnOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.32f));

    g.setColour (desk.withAlpha (0.92f));
    g.fillRect (0, 0, getWidth(), 40);
    g.setColour (amber);
    g.setFont (silkFont (11.0f, 0.05f));
    g.drawFittedText ("MIDI LEARN  -  click a control, then move or press it on your controller.\nRight-click a control to remove its link.",
                      getLocalBounds().removeFromTop (40).reduced (10, 2), juce::Justification::centred, 2);

    const int selected = ed.proc.getLearnTarget();
    for (int t = 0; t < BrumeProcessor::numMidiTargets; ++t)
    {
        auto r = ed.targetBounds (t).toFloat().expanded (5.0f);
        const int code = ed.proc.getMidiMapping (t);
        const bool round = t != 8;
        if (t == selected)
        {
            const float a = (blink / 6) % 2 == 0 ? 1.0f : 0.45f;
            g.setColour (amber.withAlpha (a));
            if (round) g.drawEllipse (r, 3.0f); else g.drawRoundedRectangle (r, 8.0f, 3.0f);
        }
        else
        {
            g.setColour (code >= 0 ? amber : silk.withAlpha (0.45f));
            if (round) g.drawEllipse (r, code >= 0 ? 2.0f : 1.0f); else g.drawRoundedRectangle (r, 8.0f, code >= 0 ? 2.0f : 1.0f);
        }
        if (code >= 0)
        {
            auto label = juce::Rectangle<float> (r.getCentreX() - 50.0f, r.getBottom() - 4.0f, 100.0f, 16.0f);
            g.setColour (desk.withAlpha (0.85f));
            g.fillRoundedRectangle (label.withSizeKeepingCentre (76.0f, 15.0f), 4.0f);
            g.setColour (amber);
            g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
            g.drawText (BrumeProcessor::describeMapping (code).upToFirstOccurrenceOf (" (", false, false), label, juce::Justification::centred);
        }
    }
}

void LearnOverlay::mouseDown (const juce::MouseEvent& e)
{
    int hit = -1;
    for (int t = 0; t < BrumeProcessor::numMidiTargets; ++t)
        if (ed.targetBounds (t).expanded (6).contains (e.getPosition())) { hit = t; break; }

    if (hit < 0)
    {
        ed.proc.setLearnTarget (-1);
        ed.readoutName = "MIDI LEARN";
        ed.readoutValue = "Click a control, then move it on your controller";
    }
    else if (e.mods.isPopupMenu())
    {
        ed.proc.clearMidiMapping (hit);
        ed.readoutName = BrumeProcessor::midiTargetName (hit);
        ed.readoutValue = "MIDI link removed";
    }
    else
    {
        ed.proc.setLearnTarget (hit);
        ed.readoutName = BrumeProcessor::midiTargetName (hit);
        ed.readoutValue = "move or press a control on your controller...";
    }
    ed.repaint();
}

void BrumeEditor::resized()
{
    const auto pb = pedalBounds();
    const float colW = pb.getWidth() / 4.0f;
    const int knobSize = 78;
    auto colX = [&] (int i) { return (int) (pb.getX() + colW * (i + 0.5f)); };

    for (int i = 0; i < 4; ++i)
        knobs[(size_t) i].slider.setBounds (juce::Rectangle<int> (knobSize, knobSize).withCentre ({ colX (i), (int) pb.getY() + 92 }));
    for (int i = 1; i < 4; ++i)
        knobs[(size_t) (3 + i)].slider.setBounds (juce::Rectangle<int> (knobSize, knobSize).withCentre ({ colX (i), (int) pb.getY() + 212 }));

    mixToggle.setBounds (juce::Rectangle<int> (60, 80).withCentre ({ colX (0), (int) pb.getY() + 210 }));
    tapSmall.setBounds (juce::Rectangle<int> (64, 22).withCentre ({ colX (3), (int) pb.getY() + 266 }));

    knobs[7].slider.setBounds (juce::Rectangle<int> (62, 62).withCentre ({ (int) pb.getCentreX(), (int) pb.getY() + 486 }));
    onSwitch.setBounds (juce::Rectangle<int> (104, 104).withCentre ({ (int) (pb.getX() + 120), (int) pb.getY() + 476 }));
    tapSwitch.setBounds (juce::Rectangle<int> (104, 104).withCentre ({ (int) (pb.getRight() - 120), (int) pb.getY() + 476 }));

    midiButton.setBounds (430, 664, 70, 40);
    recButton.setBounds (360, 664, 62, 40);
    overlay.setBounds (0, 0, kW, 656);
    updateBanner.setBounds (0, 0, kW, 40);
}

void BrumeEditor::paint (juce::Graphics& g)
{
    // desk
    g.setGradientFill (juce::ColourGradient (deskHi, kW * 0.5f, kH * 0.3f, desk, 0.0f, (float) kH, true));
    g.fillAll();

    const auto pb = pedalBounds();

    // jacks on the top edge
    {
        const char* names[] = { "OUT R", "OUT L", "9V", "IN L", "IN R" };
        for (int i = 0; i < 5; ++i)
        {
            const float x = pb.getX() + 60.0f + i * (pb.getWidth() - 120.0f) / 4.0f;
            auto jr = juce::Rectangle<float> (i == 2 ? 18.0f : 26.0f, 16.0f).withCentre ({ x, pb.getY() - 8.0f });
            if (i == 2) g.setColour (juce::Colour (0xff1e1e1e));
            else g.setGradientFill (juce::ColourGradient (juce::Colour (0xff55595a), jr.getX(), 0, juce::Colour (0xffc9cbc8), jr.getCentreX(), 0, false));
            g.fillRoundedRectangle (jr, 3.0f);
            g.setColour (inkDim);
            g.setFont (silkFont (9.0f));
            g.drawText (names[i], juce::Rectangle<float> (x - 30.0f, 2.0f, 60.0f, 14.0f), juce::Justification::centred);
        }
    }

    // enclosure
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (pb.translated (0.0f, 10.0f).expanded (2.0f), 24.0f);
    g.setColour (juce::Colour (0xff868b90));
    g.fillRoundedRectangle (pb.translated (0.0f, 4.0f), 22.0f);
    {
        juce::ColourGradient body (enclHi, pb.getX() + pb.getWidth() * 0.3f, pb.getY(), enclLo, pb.getX() + pb.getWidth() * 0.6f, pb.getBottom(), false);
        body.addColour (0.35, encl);
        g.setGradientFill (body);
        g.fillRoundedRectangle (pb, 22.0f);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawRoundedRectangle (pb.reduced (1.0f), 21.0f, 1.2f);
    }
    // brushed metal
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (pb.toNearestInt());
        juce::Random rnd (7);
        for (float y = pb.getY(); y < pb.getBottom(); y += 1.0f)
        {
            const float a = rnd.nextFloat() * 0.07f;
            g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (a));
            g.fillRect (pb.getX(), y, pb.getWidth(), 1.0f);
        }
    }
    drawScrew (g, { pb.getX() + 14, pb.getY() + 14 }, 7.0f);
    drawScrew (g, { pb.getRight() - 14, pb.getY() + 14 }, 7.0f);
    drawScrew (g, { pb.getX() + 14, pb.getBottom() - 14 }, 7.0f);
    drawScrew (g, { pb.getRight() - 14, pb.getBottom() - 14 }, 7.0f);

    // version plate
    g.setColour (silk.withAlpha (0.55f));
    g.setFont (silkFont (9.5f, 0.2f));
    g.drawText ("BRUME AUDIO  -  V" + Updater::currentVersion(), juce::Rectangle<float> (pb.getX(), pb.getBottom() - 24.0f, pb.getWidth(), 14.0f), juce::Justification::centred);

    // knob labels
    g.setColour (silk);
    g.setFont (silkFont (13.0f));
    for (auto& k : knobs)
    {
        const auto b = k.slider.getBounds().toFloat();
        const float h = k.id == brume::id::filter ? 16.0f : 18.0f;
        g.drawText (k.label, juce::Rectangle<float> (b.getCentreX() - 60.0f, b.getY() - h - 2.0f, 120.0f, h), juce::Justification::centred);
    }

    // small TAP label + LED under TEMPO
    {
        const auto t = tapSmall.getBounds().toFloat();
        g.setFont (silkFont (10.5f));
        g.setColour (silk.withAlpha (0.85f));
        g.drawText ("TAP", juce::Rectangle<float> (t.getX(), t.getY(), t.getWidth() - 22.0f, t.getHeight()), juce::Justification::centredRight);
        drawLed (g, { t.getRight() - 12.0f, t.getCentreY() }, 5.0f, ledGreen, tapLedOn);

        // sync badge: the tempo follows Traktor's MIDI clock or the host
        if (proc.getSyncSource() != 0)
        {
            const auto kb = knobs[6].slider.getBounds().toFloat();
            auto badge = juce::Rectangle<float> (kb.getX() - 96.0f, t.getY() + 1.0f, 84.0f, t.getHeight() - 2.0f);
            g.setColour (amber.withAlpha (0.18f));
            g.fillRoundedRectangle (badge, 5.0f);
            g.setColour (amber);
            g.drawRoundedRectangle (badge, 5.0f, 1.0f);
            g.setFont (silkFont (10.0f, 0.06f));
            g.drawText ("SYNC " + juce::String (proc.getSyncBpm(), 1) + "  " + brume::division (proc.apvts.getRawParameterValue (brume::id::tempo)->load()).label,
                        badge, juce::Justification::centred);
        }
    }

    // brand: black logo plate (BRUME / rule / WHITE NOISE - REVERB)
    {
        const auto plate = juce::Rectangle<float> (300.0f, 112.0f).withCentre ({ pb.getCentreX(), pb.getY() + 342.0f });
        g.setColour (enclHi.withAlpha (0.9f));                       // lower lip catching the light
        g.fillRoundedRectangle (plate.translated (0.0f, 1.5f), 16.0f);
        g.setColour (juce::Colour (0xff0c0d0e));
        g.fillRoundedRectangle (plate, 16.0f);
        g.setColour (knobHi.withAlpha (0.9f));                       // top inner highlight
        g.drawLine (plate.getX() + 14.0f, plate.getY() + 1.0f, plate.getRight() - 14.0f, plate.getY() + 1.0f, 1.0f);

        g.setColour (pointer);
        g.setFont (logoFont (38.0f, 0.115f));
        g.drawText ("BRUME", juce::Rectangle<float> (plate.getX(), plate.getY() + 20.0f, plate.getWidth() + 4.0f, 42.0f), juce::Justification::centred);
        g.setColour (knobHi);
        g.fillRect (plate.getX() + 26.0f, plate.getY() + 72.0f, plate.getWidth() - 52.0f, 1.0f);
        g.setColour (juce::Colour (0xffb4b9be));
        g.setFont (logoFont (8.5f, 0.55f));
        g.drawText (juce::String::fromUTF8 ("WHITE NOISE \xc2\xb7 REVERB"), juce::Rectangle<float> (plate.getX(), plate.getY() + 82.0f, plate.getWidth() + 5.0f, 14.0f), juce::Justification::centred);
    }

    // footswitch LEDs and labels
    const auto on = onSwitch.getBounds().toFloat();
    const auto tp = tapSwitch.getBounds().toFloat();
    drawLed (g, { on.getCentreX(), on.getY() - 12.0f }, 6.0f, ledRed, onLed);
    drawLed (g, { tp.getCentreX(), tp.getY() - 12.0f }, 6.0f, ledGreen, tapLedOn);
    g.setFont (silkFont (11.0f));
    g.setColour (silk);
    g.drawText ("ON / OFF", juce::Rectangle<float> (on.getX() - 20.0f, on.getBottom() + 4.0f, on.getWidth() + 40.0f, 14.0f), juce::Justification::centred);
    g.drawText ("TAP", juce::Rectangle<float> (tp.getX() - 20.0f, tp.getBottom() + 4.0f, tp.getWidth() + 40.0f, 14.0f), juce::Justification::centred);

    // readout
    auto ro = juce::Rectangle<float> (20.0f, 664.0f, 332.0f, 40.0f);
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillRoundedRectangle (ro, 9.0f);
    g.setColour (line);
    g.drawRoundedRectangle (ro, 9.0f, 1.0f);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.setColour (amber);
    g.drawText (readoutName, ro.reduced (14.0f, 0.0f), juce::Justification::centredLeft);
    g.setColour (ink);
    g.drawText (readoutValue, ro.reduced (14.0f, 0.0f).withTrimmedLeft (100.0f), juce::Justification::centredRight);
}
