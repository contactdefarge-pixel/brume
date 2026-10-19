#include "StationEditor.h"
#include "BinaryData.h"

using namespace station;

namespace
{
    constexpr int kW = StationEditor::baseW, kH = StationEditor::baseH;
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
    const char* kFaderAltLabel[4] = { "SEND", "TONE", "VOLUME", "OPEN" };
    const char* kKnobIds[4]    = { id::brumeLength, id::snareRate, id::patDensity, id::padChord };
    const char* kAltIds[4]     = { id::brumeChar, id::snareLevel, id::patKit, id::padTimbre };
    const char* kFaderIds[4]   = { id::brumeAmount, id::snarePitch, id::patVolume, id::padOpen };
    const char* kFaderAltIds[4] = { id::brumeSend, id::snareTone, id::patVolume, id::padOpen };
    const char* kOnIds[4]      = { id::brumeOn, id::snareOn, id::patOn, id::padOn };

    // F1 geometry (base coordinates)
    const juce::Rectangle<float> kBody { 16.0f, 16.0f, 488.0f, 968.0f };
    constexpr int padSize = 96, padGap = 14, padX0 = 47, padY0 = 486;
    constexpr int rowY[3] = { 328, 380, 432 };   // middle section: 3 rows of buttons
    inline int colCentre (int c) { return padX0 + c * (padSize + padGap) + padSize / 2; }
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
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 9.0f);
    g.setColour (juce::Colour (0xff17191b));
    g.fillRoundedRectangle (r, 9.0f);
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
    g.setFont (silkFont (11.0f, 0.06f));
    g.drawText (label, r.reduced (6.0f).removeFromBottom (18.0f), juce::Justification::centred);
}

//==============================================================================
StationEditor::StationEditor (StationProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (body);
    body.setBounds (0, 0, kW, kH);

    for (int c = 0; c < 4; ++c)
    {
        auto& col = cols[(size_t) c];
        col.knob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        col.knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        col.knob.setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
        col.knob.setPopupDisplayEnabled (true, true, this);
        body.addAndMakeVisible (col.knob);
        col.fader.setSliderStyle (juce::Slider::LinearVertical);
        col.fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        col.fader.setPopupDisplayEnabled (true, true, this);
        body.addAndMakeVisible (col.fader);
        col.stop.setButtonText (kNames[c]);
        col.stop.setClickingTogglesState (true);
        body.addAndMakeVisible (col.stop);
        col.knobAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, kKnobIds[c], col.knob);
        col.faderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, kFaderIds[c], col.fader);
        col.stopAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kOnIds[c], col.stop);
    }

    for (int i = 0; i < 16; ++i)
    {
        pads.push_back (std::make_unique<Pad> (*this, i));
        body.addAndMakeVisible (*pads.back());
    }

    // the F1's buttons send exactly what the hardware sends
    auto note = [this] (int n) { send (juce::MidiMessage::noteOn (16, n, (juce::uint8) 127)); };
    shiftBtn.setClickingTogglesState (true);   // on screen SHIFT latches (on the F1 you hold it)
    shiftBtn.onClick = [this] { send (shiftBtn.getToggleState() ? juce::MidiMessage::noteOn (16, 24, (juce::uint8) 127) : juce::MidiMessage::noteOff (16, 24)); };
    syncBtn.onClick    = [note] { note (21); };
    reverseBtn.onClick = [note] { note (25); };
    typeBtn.onClick    = [note] { note (26); };
    browseBtn.onClick  = [note] { note (28); };
    encPush.onClick    = [note] { note (29); };
    encLeft.onClick  = [this] { send (juce::MidiMessage::controllerEvent (16, 9, 127)); };
    encRight.onClick = [this] { send (juce::MidiMessage::controllerEvent (16, 9, 1)); };
    captureBtn.setEnabled (false);
    sizeBtn.setEnabled (false);
    quantBtn.setClickingTogglesState (true);
    quantAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id::quant, quantBtn);
    for (auto* b : { &syncBtn, &quantBtn, &captureBtn, &shiftBtn, &reverseBtn, &typeBtn, &sizeBtn, &browseBtn, &encLeft, &encRight, &encPush })
        body.addAndMakeVisible (*b);

    if (auto* kp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id::key)))
        keyBox.addItemList (kp->choices, 1);
    keyBox.setColour (juce::ComboBox::backgroundColourId, knobCol);
    keyBox.setColour (juce::ComboBox::textColourId, pointerCol);
    keyBox.setColour (juce::ComboBox::outlineColourId, knobHi);
    keyBox.setColour (juce::ComboBox::arrowColourId, pointerCol);
    body.addAndMakeVisible (keyBox);
    keyAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id::key, keyBox);

    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    bpmSlider.setColour (juce::Slider::textBoxTextColourId, pointerCol);
    bpmSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    body.addAndMakeVisible (bpmSlider);
    bpmAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id::bpm, bpmSlider);

    masterKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    masterKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    masterKnob.setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
    masterKnob.setPopupDisplayEnabled (true, true, this);
    body.addAndMakeVisible (masterKnob);
    masterAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id::master, masterKnob);

    layoutBody();
    addChildComponent (updateBanner);

    // resizable, keeps the F1's proportions; opens at a laptop-friendly size
    setResizable (true, true);
    setResizeLimits ((int) (kW * 0.55f), (int) (kH * 0.55f), (int) (kW * 1.3f), (int) (kH * 1.3f));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kW / (double) kH);
    setSize ((int) (kW * 0.78f), (int) (kH * 0.78f));
    startTimerHz (30);
}

StationEditor::~StationEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void StationEditor::layoutBody()
{
    // F1 top to bottom: knobs, faders, middle section, 4x4 pads, STOP buttons
    for (int c = 0; c < 4; ++c)
    {
        auto& col = cols[(size_t) c];
        const int cx = colCentre (c);
        col.knob.setBounds (juce::Rectangle<int> (66, 66).withCentre ({ cx, 90 }));
        col.fader.setBounds (juce::Rectangle<int> (60, 140).withCentre ({ cx, 222 }));
        col.stop.setBounds (juce::Rectangle<int> (padSize, 34).withCentre ({ cx, 940 }));
    }
    // middle section (columns aligned with the pads):
    //   SYNC    QUANT  CAPTURE | display
    //   REVERSE TYPE   SIZE    | encoder
    //   SHIFT                  | BROWSE
    auto cell = [] (int c, int r) { return juce::Rectangle<int> (padX0 + c * (padSize + padGap), rowY[r], padSize, 34); };
    syncBtn.setBounds (cell (0, 0));    quantBtn.setBounds (cell (1, 0)); captureBtn.setBounds (cell (2, 0));
    reverseBtn.setBounds (cell (0, 1)); typeBtn.setBounds (cell (1, 1));  sizeBtn.setBounds (cell (2, 1));
    shiftBtn.setBounds (cell (0, 2));   browseBtn.setBounds (cell (3, 2));
    const auto enc = cell (3, 1);
    encLeft.setBounds (enc.getX(), enc.getY() + 2, 26, 30);
    encPush.setBounds (enc.getX() + 28, enc.getY() - 4, 40, 42);
    encRight.setBounds (enc.getRight() - 26, enc.getY() + 2, 26, 30);

    for (int i = 0; i < 16; ++i)
        pads[(size_t) i]->setBounds (padX0 + (i % 4) * (padSize + padGap), padY0 + (i / 4) * (padSize + padGap), padSize, padSize);

    // strip under the controller: key, tempo, master
    keyBox.setBounds (190, 1000, 128, 26);
    bpmSlider.setBounds (330, 1000, 120, 26);
    masterKnob.setBounds (458, 990, 44, 44);
}

void StationEditor::resized()
{
    body.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kW));
    updateBanner.setBounds (0, 0, getWidth(), 40);
}

void StationEditor::timerCallback()
{
    // SHIFT turns knobs and faders into their second function, on screen as on the F1
    static bool lastShift = false;
    const bool shift = proc.isShiftHeld();
    if (shift != lastShift)
    {
        lastShift = shift;
        for (int c = 0; c < 4; ++c)
        {
            auto& col = cols[(size_t) c];
            col.knobAtt.reset(); col.faderAtt.reset();
            col.knobAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, (shift ? kAltIds : kKnobIds)[c], col.knob);
            col.faderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, (shift ? kFaderAltIds : kFaderIds)[c], col.fader);
        }
        shiftBtn.setToggleState (shift, juce::dontSendNotification);
    }
    typeBtn.setToggleState (proc.isStepMode(), juce::dontSendNotification);
    typeBtn.setButtonText (proc.isStepMode() ? "STEP" : "TYPE");
    bpmSlider.setEnabled (proc.getSyncSource() == 0);
    body.repaint();
}

void StationEditor::paint (juce::Graphics& g) { g.fillAll (desk); }

void StationEditor::paintBody (juce::Graphics& g)
{
    g.fillAll (desk);
    const auto pb = kBody;

    // enclosure: the F1's shape, in Brume's light brushed metal
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (pb.translated (0.0f, 8.0f), 22.0f);
    {
        juce::ColourGradient bodyGrad (enclHi, pb.getX() + pb.getWidth() * 0.3f, pb.getY(), enclLo, pb.getX() + pb.getWidth() * 0.6f, pb.getBottom(), false);
        bodyGrad.addColour (0.35, encl);
        g.setGradientFill (bodyGrad);
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

    // display: two digits like the F1 (key, build countdown or track) + beat dots
    {
        const auto d = juce::Rectangle<float> ((float) (padX0 + 3 * (padSize + padGap)), (float) rowY[0] - 6.0f, (float) padSize, 46.0f);
        g.setColour (juce::Colour (0xff0a0b0c));
        g.fillRoundedRectangle (d, 8.0f);
        g.setColour (red);
        g.setFont (logoFont (22.0f, 0.06f));
        g.drawText (proc.getDisplayText(), d.withHeight (34.0f).translated (0.0f, 1.0f), juce::Justification::centred);
        const int beatNow = (int) proc.getBarBeat();
        for (int b = 0; b < 4; ++b)
        {
            const auto dot = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ d.getX() + 18.0f + 20.0f * (float) b, d.getBottom() - 7.0f });
            g.setColour (b == beatNow ? (b == 0 ? red : amber) : juce::Colour (0xff2a2d30));
            g.fillEllipse (dot);
        }
    }
    g.setColour (silk.withAlpha (0.7f));
    g.setFont (silkFont (9.0f, 0.12f));
    g.drawText (proc.isStepMode() ? "TRACK" : "KEY", encPush.getBounds().toFloat().translated (0.0f, 42.0f).withHeight (10.0f).expanded (20.0f, 0.0f), juce::Justification::centred);
    g.drawText ("TAP", browseBtn.getBounds().toFloat().translated (0.0f, 35.0f).withHeight (13.0f), juce::Justification::centred);
    g.drawText ("SWELL", reverseBtn.getBounds().toFloat().translated (0.0f, 35.0f).withHeight (13.0f), juce::Justification::centred);
    g.drawText (proc.isStepMode() ? "PERFORM" : "STEP EDIT", typeBtn.getBounds().toFloat().translated (0.0f, 35.0f).withHeight (13.0f), juce::Justification::centred);
    g.drawText ("BAR 1", syncBtn.getBounds().toFloat().translated (0.0f, 35.0f).withHeight (13.0f), juce::Justification::centred);

    // column print: name, knob function, fader function
    const bool shift = proc.isShiftHeld();
    auto valueOf = [this] (const char* pid) { auto* prm = proc.apvts.getParameter (pid); return prm != nullptr ? prm->getCurrentValueAsText() : juce::String(); };
    for (int c = 0; c < 4; ++c)
    {
        const float cx = (float) colCentre (c);
        g.setColour (silk);
        g.setFont (logoFont (11.5f, 0.12f));
        g.drawText (kNames[c], juce::Rectangle<float> (cx - 55.0f, 34.0f, 110.0f, 16.0f), juce::Justification::centred);
        const char* kid = shift ? kAltIds[c] : kKnobIds[c];
        juce::String kv = valueOf (kid);
        if (c == 3 && ! shift)
        {
            const int key = (int) proc.apvts.getRawParameterValue (id::key)->load();
            kv = degreeName (chordDegree (proc.apvts.getRawParameterValue (id::padChord)->load()), keyIsMajor (key));
        }
        g.setFont (silkFont (10.5f, 0.08f));
        g.setColour (shift ? juce::Colour (0xffb06a00) : silk);
        g.drawText (juce::String (shift ? kAltLabel[c] : kKnobLabel[c]) + "  " + kv, juce::Rectangle<float> (cx - 55.0f, 124.0f, 110.0f, 14.0f), juce::Justification::centred);
        g.setColour (shift ? juce::Colour (0xffb06a00) : silk);
        g.drawText (juce::String (shift ? kFaderAltLabel[c] : kFaderLabel[c]) + "  " + valueOf (shift ? kFaderAltIds[c] : kFaderIds[c]),
                    juce::Rectangle<float> (cx - 55.0f, 296.0f, 110.0f, 14.0f), juce::Justification::centred);
        if (proc.isArmed (c) && std::fmod (juce::Time::getMillisecondCounterHiRes() * 0.006, 1.0) < 0.5)
        {
            g.setColour (amber);
            g.drawRoundedRectangle (cols[(size_t) c].stop.getBounds().toFloat().expanded (3.0f), 9.0f, 2.0f);
        }
    }

    // print at the bottom of the controller
    g.setColour (silk.withAlpha (0.55f));
    g.setFont (logoFont (9.0f, 0.4f));
    g.drawText ("BRUME STATION", juce::Rectangle<float> (pb.getX(), pb.getBottom() - 26.0f, pb.getWidth(), 14.0f), juce::Justification::centred);

    // strip under the controller: logo, key, tempo, master, status
    {
        const auto plate = juce::Rectangle<float> (16.0f, 990.0f, 160.0f, 48.0f);
        g.setColour (knobCol);
        g.fillRoundedRectangle (plate, 10.0f);
        g.setColour (pointerCol);
        g.setFont (logoFont (19.0f, 0.12f));
        g.drawText ("BRUME", plate.withHeight (32.0f).translated (2.0f, 3.0f), juce::Justification::centred);
        g.setColour (knobHi);
        g.fillRect (plate.getX() + 16.0f, plate.getY() + 34.0f, plate.getWidth() - 32.0f, 1.0f);
        g.setColour (juce::Colour (0xffb4b9be));
        g.setFont (logoFont (6.5f, 0.5f));
        g.drawText ("STATION  -  F1", plate.withTop (plate.getY() + 36.0f).withHeight (10.0f).translated (2.0f, 0.0f), juce::Justification::centred);

        g.setColour (juce::Colour (0xff9aa0a6));
        g.setFont (silkFont (8.5f, 0.12f));
        g.drawText ("KEY", juce::Rectangle<float> (190.0f, 1028.0f, 128.0f, 12.0f), juce::Justification::centred);
        g.drawText ("TEMPO " + juce::String (proc.apvts.getRawParameterValue (id::bpm)->load(), 1) + " (NO SYNC)",
                    juce::Rectangle<float> (330.0f, 1028.0f, 124.0f, 12.0f), juce::Justification::centred);
        g.drawText ("MASTER", juce::Rectangle<float> (450.0f, 1028.0f, 60.0f, 12.0f), juce::Justification::centred);

        const int src = proc.getSyncSource();
        juce::String sync = src == 1 ? "SYNC TRAKTOR" : src == 2 ? "SYNC HOST" : "INTERNAL";
        juce::String f1;
        switch (proc.getF1Status())
        {
            case F1Reader::connected:   f1 = "F1 CONNECTED"; break;
            case F1Reader::busy:        f1 = "F1 BUSY (Traktor?)"; break;
            case F1Reader::unsupported: f1 = "F1: app only"; break;
            default:                    f1 = "F1 NOT FOUND"; break;
        }
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        g.setColour (amber);
        g.drawText (sync + "  " + juce::String (proc.getBpm(), 1) + " BPM", juce::Rectangle<float> (20.0f, 1052.0f, 200.0f, 22.0f), juce::Justification::centredLeft);
        g.setColour (proc.getF1Status() == F1Reader::connected ? juce::Colour (0xff4cd964) : juce::Colour (0xffe8e6df));
        g.drawText (f1, juce::Rectangle<float> (190.0f, 1052.0f, 140.0f, 22.0f), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff9aa0a6));
        const auto ev = proc.getLastEvent();
        g.drawText ((ev.isNotEmpty() ? ev + "   " : juce::String()) + "V" + Updater::currentVersion(),
                    juce::Rectangle<float> (300.0f, 1052.0f, 204.0f, 22.0f), juce::Justification::centredRight);
    }
}
