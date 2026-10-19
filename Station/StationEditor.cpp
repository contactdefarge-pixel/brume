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

    const char* kNames[4]     = { "BRUME", "ROLL", "PATTERNS", "PAD" };
    const char* kOnIds[4]     = { id::brumeOn, id::rollOn, id::patOn, id::padOn };
    const char* kLevelIds[4]  = { id::brumeLevel, id::rollLevel, id::patLevel, id::padLevel };

    // F1 geometry (base coordinates)
    const juce::Rectangle<float> kBody { 16.0f, 16.0f, 488.0f, 968.0f };
    constexpr int padSize = 96, padGap = 14, padX0 = 47, padY0 = 486;
    constexpr int rowY[3] = { 328, 380, 432 };
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
namespace
{
    juce::String wikiText (int tab)
    {
        auto u = [] (const char* s) { return juce::String::fromUTF8 (s); };
        switch (tab)
        {
            case 0: return u (
"LE PRINCIPE\n\n"
"Brume Station fonctionne par PROGRAMMES. Chaque programme prend toute la F1 pour lui : les 4 boutons rotatifs "
"(8 réglages avec SHIFT), les 16 pads et l'encodeur.\n\n"
"CHOISIR UN PROGRAMME : maintenir SHIFT et appuyer sur un pad de la rangée du haut.\n"
"   SHIFT + pad 1 = BRUME     SHIFT + pad 2 = ROLL     SHIFT + pad 3 = PATTERNS\n"
"   SHIFT + pad 4 = PAD       SHIFT + pad 5 = FX\n"
"   (ou SIZE : programme suivant). L'écran affiche P1 à P5, les pads prennent la couleur du programme.\n\n"
"CE QUI NE CHANGE JAMAIS (pour mixer à tout moment) :\n"
"   - les 4 FADERS = volume de BRUME, ROLL, PATTERNS, PAD\n"
"   - les 4 boutons STOP (sous les pads) = marche / arrêt de BRUME, ROLL, PATTERNS, PAD\n\n"
"Les programmes continuent de jouer quand on change de page : on peut lancer un rythme, passer sur ROLL "
"pour faire un roulement, puis sur PAD pour changer d'accord, sans rien couper.\n\n"
"Avec QUANT allumé, tout part sur le temps (PATTERNS sur la mesure) et le bouton STOP clignote en attendant.\n\n"
"À L'ÉCRAN : SHIFT reste enfoncé d'un clic (re-cliquer pour le relâcher). Les noms en orange sous les boutons "
"rotatifs sont les fonctions SHIFT.");
            case 1: return u (
"BRUME (SHIFT + pad 1) - bruit blanc et réverbe, comme la pédale\n\n"
"BOUTONS : LENGTH (longueur de réverbe) · CHARACTER (petite pièce -> grand hall) · AIR (brillance) · "
"TREMOLO (profondeur du trémolo sur le bruit)\n"
"AVEC SHIFT : TREM RATE (vitesse calée sur le tempo) · LOW CUT (coupe les graves de Brume) · "
"SEND (quantité des autres programmes envoyée dans la réverbe) · NOISE COLOR (bruit sombre -> brillant)\n\n"
"PADS\n"
"   Rangée 1 : trémolo à 1/4, 1/8, 1/16, 1/32\n"
"   Rangée 2 : longueur de réverbe 1 s, 3 s, 6 s, 12 s\n"
"   Rangée 3 (tant qu'on appuie) : FREEZE (la réverbe tient à l'infini) · THROW (tout part dans la réverbe) · "
"NOISE (bruit même si BRUME est arrêté) · TREM ON (trémolo marche / arrêt)\n"
"   Rangée 4 : SMALL, PLATE, HALL, VAST (caractère + longueur prêts à l'emploi)\n\n"
"ENCODEUR : longueur fine.  FADER 1 : volume du bruit.  STOP 1 : marche / arrêt (la queue s'éteint selon LENGTH).\n\n"
"La réverbe de Brume sert aussi aux autres programmes (réglage REVERB de chacun).");
            case 2: return u (
"ROLL (SHIFT + pad 2) - roulements façon X-PAD du RMX-1000\n\n"
"PADS\n"
"   Rangée 1 : choisir le son : SNARE, CLAP, KICK, HAT (un appui le joue une fois)\n"
"   Rangées 2-3 : ROULEMENT TANT QU'ON APPUIE, calé sur le tempo :\n"
"      1/2, 1/4, 1/8, 1/16 / 1/32, 1/8T, 1/16T, 1/64\n"
"      Appuyer sur un autre pad change la vitesse en direct. Relâcher = stop.\n"
"   Rangée 4 : BUILD 2, 4, 8, 16 mesures : roulement automatique qui accélère, monte d'une octave, "
"s'ouvre (filtre) et s'arrête pile au drop. L'écran compte les temps.\n\n"
"HOLD (bouton CAPTURE) : les roulements restent verrouillés sans garder le doigt ; ré-appuyer sur le pad pour arrêter.\n\n"
"BOUTONS : PITCH (-12 à +12) · DECAY (son court -> long) · FILTER (gauche = passe-bas, droite = passe-haut, milieu = ouvert) · "
"REVERB (envoi dans la réverbe de Brume)\n"
"AVEC SHIFT : TONE (caisse claire grasse -> brillante) · SWING · PAN · DRIVE (saturation)\n\n"
"ENCODEUR : pitch demi-ton par demi-ton (appui = remise à 0).  FADER 2 : volume.  STOP 2 : coupe ROLL.\n\n"
"Astuce : pendant un roulement, monte PITCH et FILTER vers la droite pour une montée maison.");
            case 3: return u (
"PATTERNS (SHIFT + pad 3) - boîte à rythmes 16 pas\n\n"
"PADS : les 16 pas de l'instrument choisi (allumé = coup). Le pas en blanc = la lecture.\n"
"ENCODEUR : choisir l'instrument : 1 KICK, 2 CLAP, 3 HAT, 4 OPEN HAT, 5 PERC (l'écran affiche t1 à t5 ; "
"appui sur l'encodeur = instrument suivant)\n"
"SHIFT + ENCODEUR ou SHIFT + pads 9 à 12 : mémoire de rythme 1 à 4\n"
"SHIFT + pad 13 : effacer l'instrument · SHIFT + pad 16 : recharger le rythme d'origine de la mémoire\n"
"Mémoires d'origine : 1 house, 2 techno, 3 breaks, 4 minimal.\n\n"
"BOUTONS : DENSITY (gauche = enlève des coups, droite = en ajoute) · TUNE (accorde le kit) · SWING · "
"FILTER (passe-bas / passe-haut)\n"
"AVEC SHIFT : DECAY · DRIVE · REVERB · ACCENT (temps forts plus marqués)\n\n"
"FADER 3 : volume.  STOP 3 : marche / arrêt (démarre sur la mesure suivante).");
            case 4: return u (
"PAD (SHIFT + pad 4) - nappe d'accords dans la tonalité\n\n"
"PADS\n"
"   Rangées 1-2 : les 7 accords de la tonalité (I à VII) + I à l'octave. Un appui lance la nappe si elle est arrêtée.\n"
"   Rangée 3 : ARP (arpège marche / arrêt) · ARP 1/8 · ARP 1/16 · ARP 1/16T\n"
"   Rangée 4 : OCT -1 · OCT 0 · OCT +1 · SUS (accord suspendu, plus ouvert)\n\n"
"ENCODEUR : tonalité (roue Camelot 1-12, comme dans Traktor). SHIFT + encodeur : mineur (A) / majeur (B). "
"L'écran affiche la tonalité ; le point = majeur.\n\n"
"BOUTONS : OPEN (le filtre s'ouvre, idéal pour une montée) · TIMBRE (chaud -> aérien) · ATTACK · RELEASE\n"
"AVEC SHIFT : WIDTH (largeur stéréo) · REVERB · ARP RATE · OCTAVE\n\n"
"FADER 4 : volume.  STOP 4 : marche / arrêt (fondu selon ATTACK / RELEASE).\n\n"
"Mets la tonalité du morceau qui joue dans Traktor (même numéro Camelot) : tous les accords seront justes.");
            case 5: return u (
"FX (SHIFT + pad 5) - effets ponctuels et transitions\n\n"
"PADS\n"
"   Rangée 1 : IMPACT · CRASH · CLAP · SWELL (souffle qui monte jusqu'à la prochaine mesure)\n"
"   Rangée 2 : SWELL 2 (2 mesures) · RISER (longue montée, longueur au bouton 4) · DOWN (descente) · SUB DROP\n"
"   Rangée 3 : DROP (tout s'arrête sauf les queues de réverbe) · KILL (silence total) · TAP (tempo sans synchro) · "
"BAR 1 (le temps actuel devient le 1)\n"
"   Rangée 4 : DROP + IMPACT · DROP + CRASH · BUILD 8 · BUILD 16 (roulement ROLL automatique)\n\n"
"BOUTONS : LEVEL · REVERB · TONE (sombre -> brillant) · RISER LEN (2, 4, 8, 16 mesures)\n\n"
"Avec QUANT, IMPACT, CRASH, CLAP et SUB DROP tombent sur le temps.");
            default: return u (
"BOUTONS DU HAUT\n"
"   SYNC = BAR 1 : le temps actuel devient le premier temps de la mesure\n"
"   QUANT : départs calés sur le temps / immédiats\n"
"   CAPTURE = HOLD : verrouille les roulements de ROLL\n"
"   REVERSE = SWELL : souffle jusqu'à la prochaine mesure\n"
"   TYPE = DROP : tout s'arrête, les queues restent (accessible depuis n'importe quel programme)\n"
"   SIZE : programme suivant\n"
"   BROWSE = TAP : taper le tempo quand rien ne synchronise Brume Station\n\n"
"SYNCHRO AVEC TRAKTOR\n"
"   1. Traktor > Préférences > Controller Manager > Add > Generic MIDI, Out-Port : Traktor Virtual Output\n"
"   2. Traktor > Préférences > External Sync : cocher Enable MIDI Clock, puis lancer l'horloge (métronome)\n"
"   3. Brume Station > Options > Audio/MIDI Settings : cocher Traktor Virtual Output en entrée MIDI\n"
"   En bas de la fenêtre : SYNC TRAKTOR et le tempo.\n\n"
"LA F1\n"
"   Retirer la F1 du Controller Manager de Traktor : Brume Station la lit en USB et pilote ses couleurs "
"et son écran. En bas : F1 CONNECTED.\n\n"
"SON\n"
"   Options > Audio/MIDI Settings > Output : la sortie de ta carte son, ou BlackHole 2ch pour entrer dans "
"Traktor sur un deck en Live Input.");
        }
    }
}

StationEditor::Help::Help (StationEditor& e) : ed (e)
{
    static const char* names[] = { "PRINCIPE", "BRUME", "ROLL", "PATTERNS", "PAD", "FX", "BOUTONS" };
    for (int i = 0; i < 7; ++i)
    {
        auto* b = tabs.add (new juce::TextButton (names[i]));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { show (i); };
        addAndMakeVisible (b);
    }
    close.onClick = [this] { setVisible (false); };
    addAndMakeVisible (close);
    text.setMultiLine (true);
    text.setReadOnly (true);
    text.setScrollbarsShown (true);
    text.setCaretVisible (false);
    text.setFont (juce::Font (juce::FontOptions (14.5f)));
    text.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff111316));
    text.setColour (juce::TextEditor::textColourId, juce::Colour (0xffe8e6df));
    text.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    text.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (text);
}

void StationEditor::Help::show (int tab)
{
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState (i == tab, juce::dontSendNotification);
    text.setText (wikiText (tab), false);
    text.moveCaretToTop (false);
    setVisible (true);
    toFront (false);
}

void StationEditor::Help::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xf0111316));
    g.setColour (amber);
    g.setFont (logoFont (15.0f, 0.15f));
    g.drawText ("WIKI BRUME STATION", getLocalBounds().removeFromTop (44).reduced (14, 0), juce::Justification::centredLeft);
}

void StationEditor::Help::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (44);
    close.setBounds (top.removeFromRight (44).reduced (6));
    auto tabRow = r.removeFromTop (40).reduced (8, 4);
    const int w = tabRow.getWidth() / tabs.size();
    for (auto* b : tabs) b->setBounds (tabRow.removeFromLeft (w).reduced (2, 0));
    text.setBounds (r.reduced (10));
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
    g.setColour (bright > 0.55f ? juce::Colours::black.withAlpha (0.75f) : pointerCol.withAlpha (0.82f));
    g.setFont (silkFont (11.0f, 0.04f));
    g.drawFittedText (ed.proc.getPadLabel (index), r.reduced (6.0f).removeFromBottom (20.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
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
        col.faderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, kLevelIds[c], col.fader);
        col.stopAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kOnIds[c], col.stop);
    }
    reattachKnobs();

    for (int i = 0; i < 16; ++i)
    {
        pads.push_back (std::make_unique<Pad> (*this, i));
        body.addAndMakeVisible (*pads.back());
    }

    // the buttons send exactly what the F1 sends
    auto note = [this] (int n) { send (juce::MidiMessage::noteOn (16, n, (juce::uint8) 127)); };
    shiftBtn.setClickingTogglesState (true);   // on screen SHIFT latches (on the F1 you hold it)
    shiftBtn.onClick = [this] { send (shiftBtn.getToggleState() ? juce::MidiMessage::noteOn (16, 24, (juce::uint8) 127) : juce::MidiMessage::noteOff (16, 24)); };
    syncBtn.onClick    = [note] { note (21); };
    captureBtn.onClick = [note] { note (23); };
    reverseBtn.onClick = [note] { note (25); };
    typeBtn.onClick    = [note] { note (26); };
    sizeBtn.onClick    = [note] { note (27); };
    browseBtn.onClick  = [note] { note (28); };
    encPush.onClick    = [note] { note (29); };
    encLeft.onClick  = [this] { send (juce::MidiMessage::controllerEvent (16, 9, 127)); };
    encRight.onClick = [this] { send (juce::MidiMessage::controllerEvent (16, 9, 1)); };
    quantBtn.setClickingTogglesState (true);
    quantAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id::quant, quantBtn);
    helpBtn.onClick = [this] { help.setBounds (getLocalBounds()); help.show (1 + proc.getProgram()); };
    for (auto* b : { &syncBtn, &quantBtn, &captureBtn, &shiftBtn, &reverseBtn, &typeBtn, &sizeBtn, &browseBtn, &encLeft, &encRight, &encPush, &helpBtn })
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
    addChildComponent (help);

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

void StationEditor::reattachKnobs()
{
    const int prog = proc.getProgram();
    const bool shift = proc.isShiftHeld();
    if (prog == attachedProg && shift == attachedShift) return;
    attachedProg = prog; attachedShift = shift;
    for (int c = 0; c < 4; ++c)
    {
        auto& col = cols[(size_t) c];
        col.knobAtt.reset();
        col.knobAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, knobDef (prog, c, shift).pid, col.knob);
    }
}

void StationEditor::layoutBody()
{
    for (int c = 0; c < 4; ++c)
    {
        auto& col = cols[(size_t) c];
        const int cx = colCentre (c);
        col.knob.setBounds (juce::Rectangle<int> (66, 66).withCentre ({ cx, 90 }));
        col.fader.setBounds (juce::Rectangle<int> (60, 140).withCentre ({ cx, 222 }));
        col.stop.setBounds (juce::Rectangle<int> (padSize, 34).withCentre ({ cx, 940 }));
    }
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

    keyBox.setBounds (186, 1000, 118, 26);
    bpmSlider.setBounds (312, 1000, 112, 26);
    masterKnob.setBounds (428, 990, 40, 40);
    helpBtn.setBounds (472, 994, 34, 34);
}

void StationEditor::resized()
{
    body.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kW));
    updateBanner.setBounds (0, 0, getWidth(), 40);
    if (help.isVisible()) help.setBounds (getLocalBounds());
}

void StationEditor::timerCallback()
{
    reattachKnobs();
    shiftBtn.setToggleState (proc.isShiftHeld(), juce::dontSendNotification);
    captureBtn.setToggleState (proc.isHoldOn(), juce::dontSendNotification);
    bpmSlider.setEnabled (proc.getSyncSource() == 0);
    body.repaint();
}

void StationEditor::paint (juce::Graphics& g) { g.fillAll (desk); }

void StationEditor::paintBody (juce::Graphics& g)
{
    g.fillAll (desk);
    const auto pb = kBody;
    const int prog = proc.getProgram();
    const auto progCol = fromRgb (programColour (prog));

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

    // display
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
    // secondary names under the buttons
    g.setColour (silk.withAlpha (0.7f));
    g.setFont (silkFont (9.0f, 0.12f));
    auto under = [&] (juce::Component& b, const juce::String& t) { g.drawText (t, b.getBounds().toFloat().translated (0.0f, 35.0f).withHeight (13.0f), juce::Justification::centred); };
    under (syncBtn, "BAR 1"); under (captureBtn, "HOLD"); under (reverseBtn, "SWELL"); under (typeBtn, "DROP");
    under (sizeBtn, "NEXT PROGRAM"); under (browseBtn, "TAP");
    under (encPush, prog == progPad ? "KEY" : prog == progPatterns ? "INSTRUMENT" : prog == progRoll ? "PITCH" : prog == progBrume ? "LENGTH" : "");
    under (shiftBtn, "+ PAD = PROGRAM");

    // program banner (what the knobs and pads control now)
    {
        const auto banner = juce::Rectangle<float> ((float) padX0 + 2.0f * (padSize + padGap), (float) rowY[2] - 2.0f, (float) padSize * 2.0f + padGap - 10.0f + 0.0f, 38.0f)
                                .withX ((float) padX0 + (float) (padSize + padGap)).withWidth ((float) padSize * 2.0f + padGap);
        g.setColour (knobCol);
        g.fillRoundedRectangle (banner, 8.0f);
        g.setColour (progCol);
        g.fillRoundedRectangle (banner.withWidth (6.0f).reduced (0.0f, 6.0f).translated (8.0f, 0.0f), 3.0f);
        g.setFont (logoFont (14.0f, 0.14f));
        g.drawText ("P" + juce::String (prog + 1) + "  " + programName (prog), banner.withTrimmedLeft (20.0f), juce::Justification::centredLeft);
    }

    // knob functions of the current program (orange = SHIFT), fader = global volume
    const bool shift = proc.isShiftHeld();
    auto valueOf = [this] (const char* pid) { auto* prm = proc.apvts.getParameter (pid); return prm != nullptr ? prm->getCurrentValueAsText() : juce::String(); };
    for (int c = 0; c < 4; ++c)
    {
        const float cx = (float) colCentre (c);
        const auto& kd = knobDef (prog, c, shift);
        g.setColour (shift ? juce::Colour (0xffb06a00) : silk);
        g.setFont (logoFont (10.0f, 0.1f));
        g.drawText (kd.label, juce::Rectangle<float> (cx - 55.0f, 34.0f, 110.0f, 16.0f), juce::Justification::centred);
        g.setFont (silkFont (10.5f, 0.06f));
        g.drawText (valueOf (kd.pid), juce::Rectangle<float> (cx - 55.0f, 124.0f, 110.0f, 14.0f), juce::Justification::centred);
        g.setColour (silk);
        g.drawText (juce::String (kNames[c]) + "  " + valueOf (kLevelIds[c]), juce::Rectangle<float> (cx - 55.0f, 296.0f, 110.0f, 14.0f), juce::Justification::centred);
        if (proc.isArmed (c) && std::fmod (juce::Time::getMillisecondCounterHiRes() * 0.006, 1.0) < 0.5)
        {
            g.setColour (amber);
            g.drawRoundedRectangle (cols[(size_t) c].stop.getBounds().toFloat().expanded (3.0f), 9.0f, 2.0f);
        }
    }

    g.setColour (silk.withAlpha (0.55f));
    g.setFont (logoFont (9.0f, 0.4f));
    g.drawText ("BRUME STATION", juce::Rectangle<float> (pb.getX(), pb.getBottom() - 24.0f, pb.getWidth(), 14.0f), juce::Justification::centred);

    // strip under the controller
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
        g.setFont (silkFont (8.5f, 0.1f));
        g.drawText ("KEY", juce::Rectangle<float> (186.0f, 1028.0f, 118.0f, 12.0f), juce::Justification::centred);
        g.drawText ("TEMPO " + juce::String (proc.apvts.getRawParameterValue (id::bpm)->load(), 1), juce::Rectangle<float> (312.0f, 1028.0f, 112.0f, 12.0f), juce::Justification::centred);
        g.drawText ("MASTER", juce::Rectangle<float> (418.0f, 1028.0f, 60.0f, 12.0f), juce::Justification::centred);
        g.drawText ("WIKI", juce::Rectangle<float> (466.0f, 1028.0f, 46.0f, 12.0f), juce::Justification::centred);

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
