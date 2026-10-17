#pragma once

#include "PluginProcessor.h"
#include "Updater.h"

namespace brume::ui
{
    const juce::Colour desk      { 0xff191d20 };
    const juce::Colour deskHi    { 0xff23292d };
    const juce::Colour ink       { 0xffece6d6 };
    const juce::Colour inkDim    { 0xff9aa19a };
    const juce::Colour encl      { 0xffd2d5d8 };   // light grey metal
    const juce::Colour enclHi    { 0xffeef0f2 };
    const juce::Colour enclLo    { 0xffaeb3b8 };
    const juce::Colour silk      { 0xff1f2123 };   // print on the enclosure
    const juce::Colour pointer   { 0xfff7f7f5 };
    const juce::Colour knob      { 0xff0c0d0e };
    const juce::Colour knobHi    { 0xff3a3d41 };
    const juce::Colour ledRed    { 0xffff4d2e };
    const juce::Colour ledGreen  { 0xff5fe08a };
    const juce::Colour ledOff    { 0xff5d6166 };
    const juce::Colour amber     { 0xffffb547 };
    const juce::Colour line      { 0xff3a4246 };
}

class BrumeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

// Chrome stomp switch
class Footswitch : public juce::Button
{
public:
    Footswitch (const juce::String& name) : juce::Button (name) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

class BrumeEditor;

// Shown over the pedal in MIDI learn mode: click a control, then move a control on the MIDI controller.
class LearnOverlay : public juce::Component
{
public:
    explicit LearnOverlay (BrumeEditor& e) : ed (e) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int blink = 0;
private:
    BrumeEditor& ed;
};

class BrumeEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit BrumeEditor (BrumeProcessor&);
    ~BrumeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
   #if BRUME_TEST
    void testLearnMode (bool b) { setLearnMode (b); }
   #endif

private:
    void timerCallback() override;
    void showParam (const juce::String& paramId);
    juce::Rectangle<float> pedalBounds() const;
    juce::Rectangle<int> targetBounds (int target) const;
    void setLearnMode (bool);
    friend class LearnOverlay;

    BrumeProcessor& proc;
    BrumeLookAndFeel lnf;

    struct Knob
    {
        juce::String id, label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    std::array<Knob, 8> knobs;

    juce::ToggleButton mixToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mixAtt;

    Footswitch onSwitch { "On / Off" }, tapSwitch { "Tap" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    juce::TextButton tapSmall;

    juce::String readoutName { "BRUME" }, readoutValue { "Turn a knob, or switch the effect on with On / Off" };
    juce::String lastParam;
    bool tapLedOn = false, onLed = false;

    juce::TextButton midiButton { "MIDI" };
    juce::TextButton recButton { "REC" };
    bool wasRecording = false;
    LearnOverlay overlay { *this };
    UpdateBanner updateBanner;
    bool learnMode = false;
    int shownSync = -1; int shownBpm10 = -1;
    juce::String tempoText() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrumeEditor)
};
