#include "F1Reader.h"

#if BRUME_HAS_HIDAPI
 #include "hidapi.h"
 #if JUCE_MAC
  #include "hidapi_darwin.h"
 #endif
#endif

namespace
{
    constexpr unsigned short kVendorNI = 0x17cc;
    constexpr unsigned short kProductF1 = 0x1120;

    // bit in the 32-bit button field -> note number (0 = unused)
    int noteForBit (int bit)
    {
        if (bit >= 0 && bit < 8)   return 8 - bit;        // pads 8..1
        if (bit >= 8 && bit < 16)  return 24 - bit;       // pads 16..9
        switch (bit)
        {
            case 18: return 29;   // encoder push
            case 19: return 28;   // BROWSE
            case 20: return 27;   // SIZE
            case 21: return 26;   // TYPE
            case 22: return 25;   // REVERSE
            case 23: return 24;   // SHIFT
            case 25: return 23;   // CAPTURE
            case 26: return 22;   // QUANT
            case 27: return 21;   // SYNC
            case 28: return 20;   // play/mute 4
            case 29: return 19;   // play/mute 3
            case 30: return 18;   // play/mute 2
            case 31: return 17;   // play/mute 1
            default: return 0;
        }
    }
}

juce::String F1Reader::controlName (int type, int number)
{
    if (type == 1)
        return number <= 4 ? "F1 KNOB " + juce::String (number) : "F1 FADER " + juce::String (number - 4);
    if (number >= 1 && number <= 16) return "F1 PAD " + juce::String (number);
    if (number >= 17 && number <= 20) return "F1 MUTE " + juce::String (number - 16);
    static const char* names[] = { "F1 SYNC", "F1 QUANT", "F1 CAPTURE", "F1 SHIFT", "F1 REVERSE", "F1 TYPE", "F1 SIZE", "F1 BROWSE", "F1 ENCODER" };
    if (number >= 21 && number <= 29) return names[number - 21];
    return "F1 " + juce::String (number);
}

F1Reader::F1Reader (std::function<void (const juce::MidiMessage&)> s)
    : juce::Thread ("Brume F1 reader"), sink (std::move (s)) {}

F1Reader::~F1Reader() { stopThread (2000); }

void F1Reader::start()
{
   #if BRUME_HAS_HIDAPI
    startThread();
   #else
    status = unsupported;
   #endif
}

void F1Reader::parse (const unsigned char* d, int len)
{
    // report 0x01 = 22 bytes: [id][buttons u32][encoder][4 knobs u16][4 faders u16]; some stacks drop the id
    int base = 0;
    if (len >= 22 && d[0] == 0x01) base = 0;
    else if (len == 21) base = -1;
    else return;

    const auto u32 = [&] (int off) { off += base; return (juce::uint32) d[off] | ((juce::uint32) d[off + 1] << 8) | ((juce::uint32) d[off + 2] << 16) | ((juce::uint32) d[off + 3] << 24); };
    const auto u16 = [&] (int off) { off += base; return (int) d[off] | ((int) d[off + 1] << 8); };

    const juce::uint32 buttons = u32 (1);
    const juce::uint32 changed = buttons ^ lastButtons;
    for (int bit = 0; bit < 32; ++bit)
        if (changed & (1u << bit))
            if (const int note = noteForBit (bit))
                sink ((buttons & (1u << bit)) ? juce::MidiMessage::noteOn (channel, note, (juce::uint8) 127)
                                              : juce::MidiMessage::noteOff (channel, note));
    lastButtons = buttons;

    for (int k = 0; k < 8; ++k)
    {
        const int raw = juce::jlimit (0, 4092, u16 (6 + 2 * k));
        const int v = raw * 127 / 4092;
        if (v != lastValues[k])
        {
            if (lastValues[k] >= 0)   // skip the first report so nothing jumps at connection
                sink (juce::MidiMessage::controllerEvent (channel, k + 1, v));
            lastValues[k] = v;
        }
    }
}

void F1Reader::run()
{
   #if BRUME_HAS_HIDAPI
    if (hid_init() != 0) { status = unsupported; return; }
   #if JUCE_MAC
    hid_darwin_set_open_exclusive (0);   // share the controller with Traktor
   #endif

    while (! threadShouldExit())
    {
        hid_device* dev = nullptr;
        bool found = false;
        if (auto* list = hid_enumerate (kVendorNI, kProductF1))
        {
            // the control interface is interface 1 on the F1 (Mixxx mapping); try it first, then the others
            for (int pass = 0; pass < 2 && dev == nullptr; ++pass)
                for (auto* it = list; it != nullptr && dev == nullptr; it = it->next)
                {
                    found = true;
                    if ((it->interface_number == 1) == (pass == 0))
                        dev = hid_open_path (it->path);
                }
            hid_free_enumeration (list);
        }

        if (dev == nullptr)
        {
            status = found ? busy : notFound;
            wait (2000);
            continue;
        }

        status = connected;
        lastButtons = 0;
        for (auto& v : lastValues) v = -1;
        unsigned char buf[64];
        while (! threadShouldExit())
        {
            const int n = hid_read_timeout (dev, buf, sizeof (buf), 100);
            if (n < 0) break;           // unplugged
            if (n > 0) parse (buf, n);
        }
        hid_close (dev);
        status = notFound;
    }
    hid_exit();
   #endif
}
