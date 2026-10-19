#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "StationProcessor.h"
#include "../Source/Updater.h"

class StationLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
};

class StationEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit StationEditor (StationProcessor&);
    ~StationEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void send (const juce::MidiMessage& m) { proc.sendControl (m); }

    struct Pad : public juce::Component
    {
        StationEditor& ed; int index;
        Pad (StationEditor& e, int i) : ed (e), index (i) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { ed.send (juce::MidiMessage::noteOn (16, index + 1, (juce::uint8) 127)); }
        void mouseUp (const juce::MouseEvent&) override   { ed.send (juce::MidiMessage::noteOff (16, index + 1)); }
    };

    struct Column
    {
        juce::Slider knob, alt, fader;
        juce::TextButton mute;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAtt, altAtt, faderAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt;
        juce::String name, knobLabel, altLabel, faderLabel;
        const char* knobId; const char* altId; const char* faderId;
    };

    StationProcessor& proc;
    StationLookAndFeel lnf;
    std::array<Column, 4> cols;
    std::vector<std::unique_ptr<Pad>> pads;
    juce::TextButton shiftBtn { "SHIFT" }, typeBtn { "STEP" }, quantBtn { "QUANT" }, syncBtn { "SYNC" },
                     encLeft { "<" }, encRight { ">" }, encPush { "TRACK" };
    juce::ComboBox keyBox;
    juce::Slider bpmSlider, masterKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> quantAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAtt, masterAtt;
    UpdateBanner updateBanner;
    juce::String statusLeft, statusRight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StationEditor)
};
