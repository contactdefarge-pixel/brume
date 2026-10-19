#include "StationEditor.h"
#include "BinaryData.h"

using namespace station;

namespace
{
    constexpr int kW = 1000, kH = 664;
    const juce::Colour encl { 0xffd2d5d8 }, enclHi { 0xffeef0f2 }, enclLo { 0xffaeb3b8 }, silk { 0xff1f2123 },
                       pointerCol { 0xfff7f7f5 }, knobCol { 0xff0c0d0e }, knobHi { 0xff3a3d41 }, amber { 0xffffb547 },
                       desk { 0xff15171a }, red { 0xffff3b30 };

    juce::Font logoFont (float h, float kerning = 0.1f)
    {
        static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (BinaryData::MichromaRegular_ttf, BinaryData::MichromaRegular_ttfSize);
        juce::Font f (juce::FontOptions (tf).withHeight (h));
        f.setExtraKerningFactor (kerning);
        return f;
    }
    juce::Font silkFont (float h, float kerning = 0.1f)
    {
        juce::Font f (juce::FontOptions (h, juce::Font::bold));
        f.setExtraKerningFactor (kerning);
        return f;
    }
    juce::Colour fromRgb (juce::uint32 c) { return juce::Colour ((juce::uint8) (c >> 16), (juce::uint8) (c >> 8), (juce::uint8) c); }

    const char* kNames[4]      = { "BRUME", "SNARE", "PATTERNS", "PAD" };
    const char* kKnobLabel[4]  = { "LENGTH", "SPEED", "DENSITY", "CHORD" };
    const char* kAltLabel[4]   = { "CHARACTER", "LEVEL", "KIT", "TIMBRE" };
    const char* kFaderLabel[4] = { "NOISE", "PITCH", "VOLUME", "OPEN" };
    const char* kKnobIds[4]    = { id::brumeLength, id::snareRate, id::patDensity, id::padChord };
    const char* kAltIds[4]     = { id::brumeChar, id::snareLevel, id::patKit, id::padTimbre };
    const char* kFaderIds[4]   = { id::brumeAmount, id::snarePitch, id::patVolume, id::padOpen };
    const char* kOnIds[4]      = { id::brumeOn, id::snareOn, id::patOn, id::padOn };
}

//==============================================================================
void StationLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float size = std::min (area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float rOuter = size * 0.5f, rKnob = rOuter * 0.74f;

    g.setColour (silk.withAlpha (0.7f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
        g.drawLine ({ c.getPointOnCircumference (rOuter * 0.86f, a), c.getPointOnCircumference (rOuter * 0.98f, a) }, i % 5 == 0 ? 1.5f : 1.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillEllipse (c.x - rKnob * 1.02f, c.y - rKnob + size * 0.06f, rKnob * 2.04f, rKnob * 2.04f);
    g.setGradientFill (juce::ColourGradient (knobHi, c.x - rKnob * 0.5f, c.y - rKnob * 0.8f, knobCol, c.x + rKnob * 0.3f, c.y + rKnob, false));
    g.fillEllipse (c.x - rKnob, c.y - rKnob, rKnob * 2, rKnob * 2);
    const float rCap = rKnob * 0.8f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2e31), c.x - rCap * 0.35f, c.y - rCap * 0.55f, knobCol, c.x + rCap * 0.4f, c.y + rCap * 0.7f, true));
    g.fillEllipse (c.x - rCap, c.y - rCap, rCap * 2, rCap * 2);
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawEllipse (c.x - rCap, c.y - rCap, rCap * 2, rCap * 2, 1.0f);

    const float angle = startAngle + pos * (endAngle - startAngle);
    juce::Path ptr;
    const float pw = std::max (2.5f, rKnob * 0.19f);
    ptr.addRoundedRectangle (-pw * 0.5f, -rCap * 0.95f, pw, rCap * 0.72f, pw * 0.5f);
    g.setColour (pointerCol);
    g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c));
}

void StationLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        // small horizontal slider (tempo): slot + black cap
        const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (r.withSizeKeepingCentre (r.getWidth(), 4.0f), 2.0f);
        g.setColour (knobCol);
        g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, std::min (18.0f, r.getHeight())).withCentre ({ pos, r.getCentreY() }), 3.0f);
        juce::ignoreUnused (s);
        return;
    }
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float cx = r.getCentreX();
    // scale
    g.setColour (silk.withAlpha (0.6f));
    for (int i = 0; i <= 10; ++i)
    {
        const float ty = r.getY() + r.getHeight() * (float) i / 10.0f;
        const float len = i % 5 == 0 ? 9.0f : 5.0f;
        g.drawLine (cx - 22.0f, ty, cx - 22.0f + len, ty, 1.0f);
        g.drawLine (cx + 22.0f - len, ty, cx + 22.0f, ty, 1.0f);
    }
    // slot
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillRoundedRectangle (cx - 3.0f, r.getY() - 4.0f, 6.0f, r.getHeight() + 8.0f, 3.0f);
    // cap
    const auto cap = juce::Rectangle<float> (34.0f, 20.0f).withCentre ({ cx, pos });
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (cap.translated (0.0f, 3.0f), 4.0f);
    g.setGradientFill (juce::ColourGradient (knobHi, cap.getX(), cap.getY(), knobCol, cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 4.0f);
    g.setColour (pointerCol);
    g.fillRect (cap.getX() + 4.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 8.0f, 2.0f);
}

void StationLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 7.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2d31), r.getX(), r.getY(), knobCol, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 7.0f);
    if (over || down)
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.12f : 0.05f));
        g.fillRoundedRectangle (r, 7.0f);
    }
    // lit strip when on
    const auto led = r.removeFromTop (5.0f).reduced (10.0f, 1.0f);
    g.setColour (on ? amber : juce::Colour (0xff3a3d41));
    g.fillRoundedRectangle (led, 2.0f);
    if (on)
    {
        g.setColour (amber.withAlpha (0.25f));
        g.fillRoundedRectangle (led.expanded (3.0f, 2.0f), 4.0f);
    }
}

void StationLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (pointerCol.withAlpha (b.isEnabled() ? 0.92f : 0.4f));
    g.setFont (silkFont (12.0f, 0.12f));
    g.drawText (b.getButtonText(), b.getLocalBounds().withTrimmedTop (4), juce::Justification::centred);
}

//==============================================================================
void StationEditor::Pad::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto col = fromRgb (ed.proc.getPadColour (index));
    // rubber pad
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 9.0f);
    g.setColour (juce::Colour (0xff17191b));
    g.fillRoundedRectangle (r, 9.0f);
    // light through the rubber
    const float bright = std::max ({ col.getFloatRed(), col.getFloatGreen(), col.getFloatBlue() });
    g.setGradientFill (juce::ColourGradient (col.withMultipliedBrightness (1.2f).withAlpha (0.25f + 0.75f * bright), r.getCentreX(), r.getCentreY(),
                                             col.withAlpha (0.12f + 0.5f * bright), r.getX(), r.getY(), true));
    g.fillRoundedRectangle (r.reduced (3.0f), 7.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (r, 9.0f, 1.0f);

    juce::String label;
    if (ed.proc.isStepMode())
    {
        if (ed.proc.isShiftHeld())
            label = index < numSlots ? "SLOT " + juce::String (index + 1) : index == 12 ? "CLEAR" : index == 15 ? "RESET" : "";
        else
            label = juce::String (index + 1);
    }
    else
    {
        label = performPadName (index);
        if (index >= 8 && index < 12)
        {
            static const int deg[] = { 0, 3, 4, 5 };
            const int key = (int) ed.proc.apvts.getRawParameterValue (id::key)->load();
            label = juce::String ("CHORD ") + degreeName (deg[index - 8], keyIsMajor (key));
        }
    }
    g.setColour (bright > 0.55f ? juce::Colours::black.withAlpha (0.75f) : pointerCol.withAlpha (0.8f));
    g.setFont (silkFont (10.5f, 0.06f));
    g.drawText (label, r.reduced (6.0f).removeFromBottom (16.0f), juce::Justification::centred);
}

//==============================================================================
StationEditor::StationEditor (StationProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);

    for (int c = 0; c < 4; ++c)
    {
        auto& col = cols[(size_t) c];
        col.name = kNames[c]; col.knobLabel = kKnobLabel[c]; col.altLabel = kAltLabel[c]; col.faderLabel = kFaderLabel[c];
        col.knobId = kKnobIds[c]; col.altId = kAltIds[c]; col.faderId = kFaderIds[c];
        for (auto* s : { &col.knob, &col.alt })
        {
            s->setSliderStyle (juce::Slider::RotaryVerticalDrag);
            s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s->setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
            s->setPopupDisplayEnabled (true, true, this);
            addAndMakeVisible (*s);
        }
        col.fader.setSliderStyle (juce::Slider::LinearVertical);
        col.fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        col.fader.setPopupDisplayEnabled (true, true, this);
        addAndMakeVisible (col.fader);
        col.mute.setButtonText (kNames[c]);
        col.mute.setClickingTogglesState (true);
        addAndMakeVisible (col.mute);

        col.knobAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, col.knobId, col.knob);
        col.altAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, col.altId, col.alt);
        col.faderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, col.faderId, col.fader);
        col.muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kOnIds[c], col.mute);
    }

    for (int i = 0; i < 16; ++i)
    {
        pads.push_back (std::make_unique<Pad> (*this, i));
        addAndMakeVisible (*pads.back());
    }

    // SHIFT latches on screen (on the F1 you hold it)
    shiftBtn.setClickingTogglesState (true);
    shiftBtn.onClick = [this] { send (shiftBtn.getToggleState() ? juce::MidiMessage::noteOn (16, 24, (juce::uint8) 127) : juce::MidiMessage::noteOff (16, 24)); };
    typeBtn.onClick = [this] { send (juce::MidiMessage::noteOn (16, 26, (juce::uint8) 127)); };
    syncBtn.onClick = [this] { send (juce::MidiMessage::noteOn (16, 21, (juce::uint8) 127)); };
    encLeft.onClick = [this] { send (juce::MidiMessage::controllerEvent (16, 9, 127)); };
    encRight.onClick = [this] { send (juce::MidiMessage::controllerEvent (16, 9, 1)); };
    encPush.onClick = [this] { send (juce::MidiMessage::noteOn (16, 29, (juce::uint8) 127)); };
    quantBtn.setClickingTogglesState (true);
    quantAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id::quant, quantBtn);
    for (auto* b : { &shiftBtn, &typeBtn, &quantBtn, &syncBtn, &encLeft, &encRight, &encPush })
        addAndMakeVisible (*b);

    if (auto* kp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id::key)))
        keyBox.addItemList (kp->choices, 1);
    keyBox.setColour (juce::ComboBox::backgroundColourId, knobCol);
    keyBox.setColour (juce::ComboBox::textColourId, pointerCol);
    keyBox.setColour (juce::ComboBox::outlineColourId, knobHi);
    keyBox.setColour (juce::ComboBox::arrowColourId, pointerCol);
    addAndMakeVisible (keyBox);
    keyAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id::key, keyBox);

    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    bpmSlider.setColour (juce::Slider::textBoxTextColourId, silk);
    bpmSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (bpmSlider);
    bpmAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id::bpm, bpmSlider);

    masterKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    masterKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    masterKnob.setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
    masterKnob.setPopupDisplayEnabled (true, true, this);
    addAndMakeVisible (masterKnob);
    masterAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id::master, masterKnob);

    addChildComponent (updateBanner);
    setSize (kW, kH);
    startTimerHz (30);
}

StationEditor::~StationEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void StationEditor::resized()
{
    const int colX0 = 40, colW = 122;
    for (int c = 0; c < 4; ++c)
    {
        auto& col = cols[(size_t) c];
        const int cx = colX0 + colW * c + colW / 2;
        col.knob.setBounds (juce::Rectangle<int> (82, 82).withCentre ({ cx, 200 }));
        col.alt.setBounds (juce::Rectangle<int> (46, 46).withCentre ({ cx, 290 }));
        col.fader.setBounds (juce::Rectangle<int> (60, 176).withCentre ({ cx, 440 }));
        col.mute.setBounds (juce::Rectangle<int> (100, 44).withCentre ({ cx, 566 }));
    }
    const int padX0 = 560, padSize = 88, gap = 16, padY0 = 132;
    for (int i = 0; i < 16; ++i)
        pads[(size_t) i]->setBounds (padX0 + (i % 4) * (padSize + gap), padY0 + (i / 4) * (padSize + gap), padSize, padSize);
    const int by = 566 - 22;
    shiftBtn.setBounds (padX0, by, padSize, 44);
    typeBtn.setBounds (padX0 + (padSize + gap), by, padSize, 44);
    quantBtn.setBounds (padX0 + 2 * (padSize + gap), by, padSize, 44);
    syncBtn.setBounds (padX0 + 3 * (padSize + gap), by, padSize, 44);

    keyBox.setBounds (448, 70, 150, 26);
    encLeft.setBounds (610, 68, 34, 30);
    encPush.setBounds (646, 68, 70, 30);
    encRight.setBounds (718, 68, 34, 30);
    bpmSlider.setBounds (764, 70, 150, 26);
    masterKnob.setBounds (920, 56, 48, 48);
    updateBanner.setBounds (0, 0, kW, 40);
}

void StationEditor::timerCallback()
{
    typeBtn.setToggleState (proc.isStepMode(), juce::dontSendNotification);
    typeBtn.setButtonText (proc.isStepMode() ? "STEP" : "PERFORM");
    encPush.setButtonText (proc.isStepMode() ? juce::String (trackName (proc.getSelectedTrack())) : "KEY");
    if (! shiftBtn.getToggleState() && proc.isShiftHeld() != shiftBtn.getToggleState()) {}
    bpmSlider.setEnabled (proc.getSyncSource() == 0);
    for (auto& pd : pads) pd->repaint();
    repaint (0, 0, kW, 120);
    repaint (0, kH - 50, kW, 50);
    for (auto& c : cols) c.mute.repaint();
}

void StationEditor::paint (juce::Graphics& g)
{
    g.fillAll (desk);
    const auto pb = juce::Rectangle<float> (16.0f, 16.0f, (float) kW - 32.0f, 592.0f);

    // enclosure: light brushed metal, like the Brume pedal
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (pb.translated (0.0f, 8.0f), 22.0f);
    {
        juce::ColourGradient body (enclHi, pb.getX() + pb.getWidth() * 0.3f, pb.getY(), enclLo, pb.getX() + pb.getWidth() * 0.6f, pb.getBottom(), false);
        body.addColour (0.35, encl);
        g.setGradientFill (body);
        g.fillRoundedRectangle (pb, 20.0f);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (pb.toNearestInt());
        juce::Random rnd (11);
        for (float y = pb.getY(); y < pb.getBottom(); y += 1.0f)
        {
            g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (rnd.nextFloat() * 0.06f));
            g.fillRect (pb.getX(), y, pb.getWidth(), 1.0f);
        }
    }

    // logo plate
    {
        const auto plate = juce::Rectangle<float> (40.0f, 34.0f, 230.0f, 70.0f);
        g.setColour (knobCol);
        g.fillRoundedRectangle (plate, 12.0f);
        g.setColour (pointerCol);
        g.setFont (logoFont (26.0f, 0.12f));
        g.drawText ("BRUME", plate.withHeight (44.0f).translated (2.0f, 6.0f), juce::Justification::centred);
        g.setColour (knobHi);
        g.fillRect (plate.getX() + 20.0f, plate.getY() + 48.0f, plate.getWidth() - 40.0f, 1.0f);
        g.setColour (juce::Colour (0xffb4b9be));
        g.setFont (logoFont (8.0f, 0.55f));
        g.drawText ("STATION  -  F1", plate.withTop (plate.getY() + 52.0f).withHeight (14.0f).translated (3.0f, 0.0f), juce::Justification::centred);
    }

    // display, like the F1's two digits (+ the key, the bar position)
    {
        const auto d = juce::Rectangle<float> (292.0f, 34.0f, 140.0f, 70.0f);
        g.setColour (juce::Colour (0xff0a0b0c));
        g.fillRoundedRectangle (d, 8.0f);
        g.setColour (red);
        g.setFont (logoFont (30.0f, 0.06f));
        g.drawText (proc.getDisplayText(), d.withHeight (48.0f).translated (0.0f, 4.0f), juce::Justification::centred);
        // beat dots
        const int beatNow = (int) proc.getBarBeat();
        for (int b = 0; b < 4; ++b)
        {
            const auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ d.getX() + 34.0f + 24.0f * (float) b, d.getBottom() - 12.0f });
            g.setColour (b == beatNow ? (b == 0 ? red : amber) : juce::Colour (0xff2a2d30));
            g.fillEllipse (dot);
        }
    }

    // status line in the header
    {
        g.setColour (silk);
        g.setFont (silkFont (12.5f, 0.06f));
        const int src = proc.getSyncSource();
        juce::String sync = src == 1 ? "SYNC TRAKTOR (MIDI clock)" : src == 2 ? "SYNC HOST" : "INTERNAL TEMPO";
        g.drawText (sync + "   " + juce::String (proc.getBpm(), 1) + " BPM", juce::Rectangle<float> (448.0f, 36.0f, 330.0f, 20.0f), juce::Justification::centredLeft);
        juce::String f1;
        switch (proc.getF1Status())
        {
            case F1Reader::connected:   f1 = "F1 CONNECTED"; break;
            case F1Reader::busy:        f1 = "F1 BUSY (Traktor?)"; break;
            case F1Reader::unsupported: f1 = "F1: app only"; break;
            default:                    f1 = "F1 NOT FOUND"; break;
        }
        g.setColour (proc.getF1Status() == F1Reader::connected ? juce::Colour (0xff1c7c34) : silk.withAlpha (0.7f));
        g.drawText (f1, juce::Rectangle<float> (760.0f, 36.0f, 206.0f, 20.0f), juce::Justification::centredRight);
        g.setColour (silk.withAlpha (0.65f));
        g.setFont (silkFont (9.5f, 0.12f));
        g.drawText ("KEY", juce::Rectangle<float> (448.0f, 96.0f, 150.0f, 14.0f), juce::Justification::centred);
        g.drawText (proc.isStepMode() ? "TRACK (ENCODER)" : "KEY (ENCODER)", juce::Rectangle<float> (610.0f, 96.0f, 142.0f, 14.0f), juce::Justification::centred);
        g.drawText ("TEMPO (NO SYNC)", juce::Rectangle<float> (764.0f, 96.0f, 150.0f, 14.0f), juce::Justification::centredLeft);
        g.drawText ("MASTER", juce::Rectangle<float> (908.0f, 96.0f, 72.0f, 14.0f), juce::Justification::centred);
    }

    // column print
    for (int c = 0; c < 4; ++c)
    {
        auto& col = cols[(size_t) c];
        const float cx = (float) col.knob.getBounds().getCentreX();
        g.setColour (silk);
        g.setFont (logoFont (12.5f, 0.12f));
        g.drawText (col.name, juce::Rectangle<float> (cx - 60.0f, 130.0f, 120.0f, 18.0f), juce::Justification::centred);
        g.setFont (silkFont (11.0f, 0.1f));
        auto valueOf = [this] (const char* pid) { auto* prm = proc.apvts.getParameter (pid); return prm != nullptr ? prm->getCurrentValueAsText() : juce::String(); };
        juce::String kv = valueOf (col.knobId);
        if (c == 3)
        {
            const int key = (int) proc.apvts.getRawParameterValue (id::key)->load();
            kv = degreeName (chordDegree (proc.apvts.getRawParameterValue (id::padChord)->load()), keyIsMajor (key));
        }
        g.drawText (col.knobLabel + "  " + kv, juce::Rectangle<float> (cx - 60.0f, 242.0f, 120.0f, 14.0f), juce::Justification::centred);
        g.setColour (silk.withAlpha (0.7f));
        g.setFont (silkFont (9.5f, 0.1f));
        g.drawText ("SHIFT: " + col.altLabel, juce::Rectangle<float> (cx - 60.0f, 314.0f, 120.0f, 12.0f), juce::Justification::centred);
        g.setColour (silk);
        g.setFont (silkFont (11.0f, 0.1f));
        g.drawText (col.faderLabel + "  " + valueOf (col.faderId), juce::Rectangle<float> (cx - 60.0f, 530.0f, 120.0f, 14.0f), juce::Justification::centred);
        if (proc.isArmed (c) && std::fmod (juce::Time::getMillisecondCounterHiRes() * 0.006, 1.0) < 0.5)
        {
            g.setColour (amber);
            g.drawRoundedRectangle (col.mute.getBounds().toFloat().expanded (3.0f), 9.0f, 2.0f);
        }
    }
    // separator between columns and pads
    g.setColour (juce::Colours::black.withAlpha (0.12f));
    g.fillRect (536.0f, 130.0f, 1.0f, 460.0f);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillRect (537.0f, 130.0f, 1.0f, 460.0f);

    // bottom status
    {
        const auto ro = juce::Rectangle<float> (16.0f, (float) kH - 46.0f, (float) kW - 32.0f, 34.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (ro, 8.0f);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.setColour (amber);
        const auto ev = proc.getLastEvent();
        g.drawText (ev.isNotEmpty() ? ev : juce::String ("BRUME STATION"), ro.reduced (14.0f, 0.0f), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xffe8e6df));
        juce::String help = proc.isStepMode()
            ? "STEP: pads = 16 steps of " + juce::String (trackName (proc.getSelectedTrack())) + "  -  encoder = track  -  SHIFT + pad 1-4 = slot, 13 = clear, 16 = reset"
            : "PERFORM: MUTE = start/stop a column  -  SHIFT + knob/fader = second function  -  TYPE = step editor";
        g.drawText (help + "   V" + Updater::currentVersion(), ro.reduced (14.0f, 0.0f).withTrimmedLeft (170.0f), juce::Justification::centredRight);
    }
}
