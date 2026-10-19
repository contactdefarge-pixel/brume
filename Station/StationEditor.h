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

// The window follows the Kontrol F1 itself, top to bottom: SYNC / QUANT / CAPTURE + display + encoder,
// SHIFT / REVERSE / TYPE / SIZE / BROWSE, 4 knobs, 4x4 pads, 4 STOP buttons, 4 faders.
// Everything is laid out at a fixed base size and scaled with the window (resizable).
class StationEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit StationEditor (StationProcessor&);
    ~StationEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseW = 520, baseH = 1080;

private:
    void timerCallback() override;
    void send (const juce::MidiMessage& m) { proc.sendControl (m); }
    void paintBody (juce::Graphics&);
    void layoutBody();

    struct Body : public juce::Component
    {
        StationEditor& ed;
        explicit Body (StationEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override { ed.paintBody (g); }
    };

    struct Pad : public juce::Component
    {
        StationEditor& ed; int index;
        Pad (StationEditor& e, int i) : ed (e), index (i) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { ed.send (juce::MidiMessage::noteOn (16, index + 1, (juce::uint8) 127)); }
        void mouseUp (const juce::MouseEvent&) override   { ed.send (juce::MidiMessage::noteOff (16, index + 1)); }
    };

    // in-app wiki: how everything works, one tab per program
    struct Help : public juce::Component
    {
        StationEditor& ed;
        juce::TextEditor text;
        juce::OwnedArray<juce::TextButton> tabs;
        juce::TextButton close { "X" };
        explicit Help (StationEditor& e);
        void show (int tab);
        void paint (juce::Graphics&) override;
        void resized() override;
    };

    struct Column
    {
        juce::Slider knob, fader;
        juce::TextButton stop;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAtt, faderAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stopAtt;
    };

    StationProcessor& proc;
    StationLookAndFeel lnf;
    Body body { *this };
    std::array<Column, 4> cols;
    std::vector<std::unique_ptr<Pad>> pads;
    juce::TextButton syncBtn { "SYNC" }, quantBtn { "QUANT" }, captureBtn { "CAPTURE" },
                     shiftBtn { "SHIFT" }, reverseBtn { "REVERSE" }, typeBtn { "TYPE" }, sizeBtn { "SIZE" }, browseBtn { "BROWSE" },
                     encLeft { "<" }, encRight { ">" }, encPush { "PUSH" }, helpBtn { "?" };
    juce::ComboBox keyBox;
    juce::Slider bpmSlider, masterKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> quantAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAtt, masterAtt;
    UpdateBanner updateBanner;
    Help help { *this };
    int attachedProg = -1; bool attachedShift = false;
    void reattachKnobs();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StationEditor)
};
