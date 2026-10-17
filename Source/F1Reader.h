#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <functional>
#include <atomic>

// Reads a Native Instruments Traktor Kontrol F1 directly over USB (HID), even while it is in Traktor mode,
// and turns its controls into MIDI messages on channel 16 so MIDI learn works as usual.
// Report layout from the Mixxx project's F1 HID mapping (report 0x01: buttons, knobs and faders).
class F1Reader : private juce::Thread
{
public:
    enum Status { notFound = 0, connected = 1, busy = 2, unsupported = 3 };

    explicit F1Reader (std::function<void (const juce::MidiMessage&)> sink);
    ~F1Reader() override;

    void start();
    Status getStatus() const { return (Status) status.load(); }

    static constexpr int channel = 16;
    static juce::String controlName (int type, int number);   // type 1 = CC, 2 = note

   #if BRUME_TEST
    void testParse (const unsigned char* d, int len) { parse (d, len); }
   #endif

private:
    void run() override;
    void parse (const unsigned char* data, int len);

    std::function<void (const juce::MidiMessage&)> sink;
    std::atomic<int> status { notFound };
    juce::uint32 lastButtons = 0;
    int lastValues[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
};
