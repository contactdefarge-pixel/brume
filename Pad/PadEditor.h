#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PadProcessor.h"
#include "../Source/Updater.h"

class PadLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PadLookAndFeel();
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
};

// The window looks like the LPD8 MK2; labels follow the selected program.
class PadEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PadEditor (PadProcessor&);
    ~PadEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseW = 1372, baseH = 360;   // controller on the left, info panel on the right (wide and short, fits above / below Traktor)

private:
    void timerCallback() override;
    void paintBody (juce::Graphics&);
    void layoutBody();

    struct Body : public juce::Component
    {
        PadEditor& ed;
        explicit Body (PadEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override { ed.paintBody (g); }
    };

    struct PadButton : public juce::Component
    {
        PadEditor& ed; int index;   // 0..7 = PAD 1..8
        PadButton (PadEditor& e, int i) : ed (e), index (i) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
    };

    struct Help : public juce::Component
    {
        juce::TextEditor text;
        juce::OwnedArray<juce::TextButton> tabs;
        juce::TextButton close { "X" };
        Help();
        void show (int tab);
        void paint (juce::Graphics&) override;
        void resized() override;
    };

    PadProcessor& proc;
    PadLookAndFeel lnf;
    Body body { *this };
    std::vector<std::unique_ptr<PadButton>> pads;
    std::array<juce::Slider, 8> knobs;
    juce::TextButton progBtn { "PROGRAM" }, noteBtn { "NOTE" }, pcBtn { "PROG CHNG" }, ccBtn { "CC" },
                     setupBtn { "SETUP LPD8" }, helpBtn { "WIKI" },
                     fxMusicBtn { "MUSIQUE + SONS" }, fxOnlyBtn { "SONS SEULS" };
    juce::Slider bpmSlider, masterKnob, offsetSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAtt, masterAtt, offsetAtt;
    UpdateBanner updateBanner;
    Help help;
    bool programSelectMode = false;
    int shownProg = -1;
    float meter[2] {}, peakHold[2] {}; int holdTicks[2] {}; int clipTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadEditor)
};
