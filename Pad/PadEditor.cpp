#include "PadEditor.h"
#include "BinaryData.h"

using namespace bpad;

namespace
{
    constexpr int kW = PadEditor::baseW, kH = PadEditor::baseH;
    const juce::Colour chassis { 0xff1c1d1f }, chassisHi { 0xff2a2b2e }, trim { 0xffc8202a }, print { 0xffe9e9e6 },
                       printDim { 0xff8d9096 }, orange { 0xffff7a1a }, padRubber { 0xff121314 }, desk { 0xff0d0e10 };

    juce::Font logoFont (float h, float kerning = 0.1f)
    {
        static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (BinaryData::MichromaRegular_ttf, BinaryData::MichromaRegular_ttfSize);
        juce::Font f (juce::FontOptions (tf).withHeight (h));
        f.setExtraKerningFactor (kerning);
        return f;
    }
    juce::Font labelFont (float h, bool bold = true, float kerning = 0.04f)
    {
        juce::Font f (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
        f.setExtraKerningFactor (kerning);
        return f;
    }

    // geometry (base coordinates)
    const juce::Rectangle<float> kBody { 14.0f, 14.0f, 1032.0f, 330.0f };
    constexpr int padSize = 90, padGap = 16, padX0 = 268, padTopY = 62, padBotY = 200;
    constexpr int knobSize = 58, knobGap = 22, knobX0 = 704, knobTopY = 86, knobBotY = 224;

    juce::Rectangle<int> padBounds (int index)   // index 0..7 = PAD 1..8 (1-4 bottom row, 5-8 top row)
    {
        const int col = index % 4, y = index < 4 ? padBotY : padTopY;
        return { padX0 + col * (padSize + padGap), y, padSize, padSize };
    }
    juce::Rectangle<int> knobBounds (int k)      // K1-K4 top, K5-K8 bottom
    {
        const int col = k % 4, y = k < 4 ? knobTopY : knobBotY;
        return { knobX0 + col * (knobSize + knobGap), y, knobSize, knobSize };
    }

    juce::Colour padColour (const PadProcessor& p, int prog, int pad)
    {
        const float bb = p.getBarBeat();
        const bool onBeat = bb - std::floor (bb) < 0.2f;
        const bool down = p.padIsDown (pad);
        if (prog == 0)
        {
            juce::ignoreUnused (down);
            if (pad >= 4) return p.getActiveSound() == pad - 4 ? (onBeat ? juce::Colour (0xffffc070) : juce::Colour (0xffff8a1f)) : juce::Colour (0xff4a2408);
            if (p.getActiveSpeed() == pad) return p.getActiveSound() >= 0 && onBeat ? juce::Colour (0xff9fe0ff) : juce::Colour (0xff3aa8ff);
            return juce::Colour (0xff0c2a48);
        }
        if (prog == 1)
        {
            switch (pad)
            {
                case 4: return p.isBrumeOn() ? juce::Colour (0xffff2020) : juce::Colour (0xff400808);
                case 5: return onBeat ? juce::Colour (0xff40ff60) : juce::Colour (0xff0c3014);
                case 6: return p.getKnobValue (1, 5) > 0.05f ? juce::Colour (0xffb050ff) : juce::Colour (0xff2a1040);
                case 7: return down ? juce::Colours::white : juce::Colour (0xff383838);
                default: return juce::Colour (0xff141414);   // bottom row unused in this program
            }
        }
        if (prog == 2)
        {
            const int step = p.getPatternPage() * 8 + pad;
            const bool on = (p.getPatternSteps() >> step) & 1;
            const bool here = p.getPatternPlayStep() == step;
            if (here) return on ? juce::Colours::white : juce::Colour (0xff606060);
            const bool beatStart = step % 4 == 0;
            return on ? (beatStart ? juce::Colour (0xffff3a3a) : juce::Colour (0xffff8a1f))
                      : (beatStart ? juce::Colour (0xff3a1010) : juce::Colour (0xff2a1a08));
        }
        return juce::Colour (0xff202020);
    }

    juce::String wikiText (int tab)
    {
        auto u = [] (const char* s) { return juce::String::fromUTF8 (s); };
        switch (tab)
        {
            case 0: return u (
"LE PRINCIPE\n\n"
"Brume Pad transforme un Akai LPD8 MK2 en boîte d'effets façon RMX-1000 / DJM-V10 pour Traktor.\n\n"
"- La musique de Traktor passe dans Brume Pad (Traktor -> BlackHole -> Brume Pad -> enceintes) : les échos, filtres "
"et réverbes s'appliquent au morceau, comme un RMX-1000 branché entre la table et les enceintes.\n"
"- Le LPD8 a 4 PROGRAMMES. Sur le contrôleur : maintenir PROGRAM et appuyer sur PAD 1, 2, 3 ou 4.\n"
"     Programme 1 = X-PAD (kick, snare, clap, hi-hat en roulements) + effets Send du DJM-V10\n"
"     Programme 2 = BRUME (bruit blanc du Revolo, réverbe, trémolo)\n"
"     Programme 3 = PATTERN PLAYER (boîte à rythme 16 pas, comme dans Traktor)\n"
"  Brume Pad reconnaît tout seul le programme utilisé (après le SETUP LPD8) et l'écran change ses étiquettes.\n"
"- À l'écran : cliquer sur PROGRAM puis sur un des pads 1 à 4, ou utiliser les pads et boutons à la souris.\n"
"- Tout est calé sur le tempo de Traktor grâce à l'horloge MIDI (onglet SYNCHRO).\n"
"- KILL FX coupe tous les effets d'un coup en laissant les queues d'écho et de réverbe s'éteindre.\n"
"- Le commutateur FX (sous les boutons de gauche) :\n"
"     FX : MUSIQUE + SONS = les effets (delays, dub echo, réverbe, Brume, DUCK) s'appliquent au morceau ET aux sons de Brume Pad.\n"
"     FX : SONS SEULS = le morceau passe intact ; seuls les roulements, le bruit blanc et le pattern sont joués et traités par les effets.");
            case 1: return u (
"PROGRAMME 1 - X-PAD + EFFETS\n\n"
"PADS DU HAUT (PAD 5 à 8) : KICK, SNARE, CLAP, HAT. Un appui lance le roulement, un deuxième appui l'arrête. "
"Appuyer sur un autre son le remplace sans couper.\n"
"PADS DU BAS (PAD 1 à 4) : la vitesse, calée sur le tempo : 1/4, 1/8, 1/16, 1/32. Un appui la choisit (elle reste "
"allumée) ; ré-appuyer sur la vitesse allumée coupe le roulement. Changer de pad change la vitesse en direct "
"(accélérer avant le drop). Pas besoin de garder le doigt.\n\n"
"BOUTONS\n"
"   K1 DRUMS VOL : volume des roulements. Monter avant le drop, baisser au drop.\n"
"   K2 PITCH : hauteur des sons (-12 à +12). Monter pendant un roulement = montée.\n\n"
"   EFFETS SEND FAÇON DJM-V10 (K3 à K6 = quantité de musique envoyée dans chaque effet) :\n"
"   K3 SHORT DELAY : delay court et serré.\n"
"   K4 LONG DELAY : delay long, calé sur le tempo.\n"
"   K5 DUB ECHO : écho sombre qui se fond en réverbe, comme sur la V10 / 900NXS2.\n"
"   K6 REVERB : réverbe.\n"
"   K7 SIZE / FEEDBACK : pour les delays et le dub echo, la durée des répétitions (à fond = elles continuent "
"jusqu'au KILL FX) ; pour la réverbe, la taille de la pièce (à fond = grande salle vide, plus de graves).\n"
"   K8 TIME : la durée. Short delay de 1/32 à 1/8 de temps, long delay de 1/4 à 1 temps, dub echo de 1/8 à "
"1 temps, réverbe de courte à très longue. Tout est calé sur le tempo de Traktor.\n\n"
"Baisser un envoi (K3 à K6) laisse la queue sonner. KILL FX ferme les 4 envois et réduit le feedback pour que les "
"queues s'éteignent toutes seules.\n\n"
"EXEMPLE DE DROP : SNARE (le roulement part en 1/4), puis 1/8, 1/16, 1/32 en montant K1 et K2 ; au drop, "
"ré-appuyer sur SNARE (ou baisser K1).");
            case 2: return u (
"PROGRAMME 2 - BRUME\n\n"
"Le bruit blanc est voicé comme le Teile Revolo (mesuré sur tes vidéos : bruit large de 170 Hz à 10 kHz, "
"gauche et droite indépendants, qui s'épaissit dans la réverbe).\n\n"
"PADS\n"
"   PAD 5 ON / OFF : allume / éteint Brume (la queue s'éteint selon LENGTH)\n"
"   PAD 6 TAP : taper le tempo du trémolo (sans synchro)\n"
"   PAD 7 TREM ON : trémolo marche / arrêt\n"
"   PAD 8 HOLD : Brume allumé tant qu'on appuie\n"
"   PADS 1 à 4 (rangée du bas) : inactifs dans ce programme. La division du trémolo se règle avec K7 TEMPO.\n\n"
"BOUTONS (comme la pédale Brume)\n"
"   K1 REVERB (quantité de musique envoyée dans la réverbe) · K2 CHARACTER · K3 LENGTH · K4 AIR\n"
"   K5 NOISE (volume du bruit) · K6 TREMOLO · K7 TEMPO (division quand c'est synchronisé) · K8 FILTER\n\n"
"Baisser NOISE fait comme OFF : le bruit s'efface au rythme de la queue de réverbe (réglée par LENGTH).");
            case 3: return u (
"PROGRAMME 3 - PATTERN PLAYER\n\n"
"Une boîte à rythme de 16 pas (une mesure en doubles-croches), calée sur le tempo et qui repart toujours sur le 1er temps, "
"comme le Pattern Player de Traktor.\n\n"
"LES 16 PAS AVEC 8 PADS\n"
"   Mode NOTE (bouton NOTE du LPD8) : les pads sont les pas 1 à 8 (1re moitié de la mesure).\n"
"   Mode PROG CHNG (bouton PROG CHNG du LPD8) : les mêmes pads sont les pas 9 à 16.\n"
"   Un appui allume / éteint le pas. Le pas en cours de lecture clignote en blanc ; les pas rouges tombent sur les temps.\n"
"   À l'écran : NOTE / PROG CHNG choisissent la moitié affichée, puis cliquer sur les pads.\n\n"
"BOUTONS\n"
"   K1 VOLUME : à zéro, rien ne joue. C'est le bouton pour lancer / couper le pattern.\n"
"   K2 PITCH : hauteur du son (-12 à +12).\n"
"   K3 DECAY : longueur du son (court = sec, à fond = son entier).\n"
"   K4 DUCK : baisse la musique à chaque coup (effet pompe). Seulement en mode FX : MUSIQUE + SONS.\n"
"   K5 KIT : 808, 909 ou RMX (les sons extraits de ton RMX-1000).\n"
"   K6 SOUND : KICK, SNARE, CLAP, CL HAT, OP HAT, RIM, TOM, CYMBAL.\n"
"   K7 PATTERN : 8 patterns de départ (4 temps, contretemps, backbeat, croches, doubles, syncopé, shuffle, fill). "
"Tourner K7 charge le pattern ; tes modifications de pas restent tant que tu ne le tournes pas.\n"
"   K8 SWING : retarde les pas pairs (groove shuffle).\n\n"
"ASTUCE : choisir CL HAT + PATTERN 2 (contretemps) pour ajouter un charley sur un morceau, ou CLAP + PATTERN 3 avant un drop.");
            case 4: return u (
"KILL FX\n\n"
"Coupe tous les effets d'un coup : short delay, long delay, dub echo, réverbe, et Brume. Les échos et réverbes "
"gardent leur queue (le feedback est réduit pour qu'elle s'éteigne), rien ne s'arrête brutalement.\n\n"
"TROIS FAÇONS DE LE DÉCLENCHER\n"
"   1. Sur le LPD8 : appuyer sur CC puis sur n'importe quel pad (en mode CC les pads envoient des messages CC, "
"que Brume Pad interprète comme KILL FX). Appuyer ensuite sur NOTE pour revenir aux pads normaux.\n"
"   2. Sur le LPD8 : appuyer en même temps sur PAD 5 et PAD 8.\n"
"   3. À l'écran : le bouton CC (KILL FX).\n\n"
"Le bouton CC seul n'envoie rien (c'est un changement de mode du contrôleur), d'où la combinaison CC + pad.\n\n"
"Après un KILL, un effet reste coupé même si son bouton est encore tourné : il revient dès que tu bouges ce bouton.");
            case 5: return u (
"SETUP LPD8 (une seule fois)\n\n"
"Chaque programme du LPD8 envoie ses propres numéros de notes et de boutons. Le SETUP les apprend, ensuite "
"Brume Pad sait toujours sur quel programme tu es.\n\n"
"   1. Cliquer sur SETUP LPD8.\n"
"   2. Sur le LPD8 : PROGRAM + PAD 1 (programme 1), puis appuyer sur PAD 1, puis tourner K1.\n"
"   3. PROGRAM + PAD 2 (programme 2), puis appuyer sur PAD 1, puis tourner K1.\n"
"   4. PROGRAM + PAD 3 (programme 3), puis appuyer sur PAD 1, puis tourner K1.\n"
"C'est tout. Le LPD8 doit être en mode NOTE (bouton NOTE).\n\n"
"Sans SETUP, Brume Pad fonctionne quand même avec les réglages d'usine du LPD8 MK2, et le programme se choisit à l'écran.\n\n"
"Le LPD8 et le port d'horloge de Traktor sont ouverts automatiquement par l'app (rien à cocher).");
            default: return u (
"SYNCHRO TRAKTOR\n"
"   LE PLUS SIMPLE : ABLETON LINK. Dans Traktor, appuyer sur LINK (en haut de l'écran). C'est tout : Brume Pad "
"rejoint la session Link tout seul (en bas : SYNC LINK et LINK 1 PEER). Le Mac peut demander d'autoriser Brume Pad "
"sur le réseau local : accepter.\n\n"
"   OU L'HORLOGE MIDI :\n"
"   1. Traktor > Préférences > Controller Manager > Add... > Generic MIDI, Out-Port : Traktor Virtual Output.\n"
"   2. Traktor > Préférences > External Sync : cocher Enable MIDI Clock.\n"
"   3. Dans Traktor, panneau Master Clock (métronome) : lancer l'horloge.\n"
"   Brume Pad ouvre Traktor Virtual Output tout seul. En bas : SYNC MIDI CLOCK et le tempo.\n\n"
"   CALAGE FIN : si les roulements arrivent un peu avant ou après le temps de la musique, régler SYNC OFFSET "
"en bas (+ = roulements plus tard, - = plus tôt ; double-clic = 0). Ne pas décaler le morceau dans Traktor.\n\n"
"SON (la musique passe dans Brume Pad)\n"
"   - Traktor > Préférences > Audio Setup : un périphérique agrégé (carte son ou enceintes du Mac + BlackHole 2ch), "
"Output Routing : Master sur les canaux de BlackHole (les casques restent sur ta carte son).\n"
"   - Brume Pad > Options > Audio/MIDI Settings : Input = BlackHole 2ch, Output = ta carte son ou les enceintes, "
"même fréquence (44100 Hz) que Traktor.\n"
"   - Autoriser le micro pour Brume Pad (Réglages Système > Confidentialité > Microphone) sinon l'entrée reste muette.");
        }
    }
}

//==============================================================================
void PadLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float size = std::min (area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float rOuter = size * 0.5f, r = rOuter * 0.78f;
    // dots for MIN / MAX like the LPD8 print
    g.setColour (printDim);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
        const auto pt = c.getPointOnCircumference (rOuter * 0.98f, a);
        g.fillEllipse (pt.x - 1.1f, pt.y - 1.1f, 2.2f, 2.2f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (c.x - r, c.y - r + 3.0f, r * 2, r * 2);
    // knurled black knob
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3c40), c.x - r * 0.5f, c.y - r * 0.7f, juce::Colour (0xff0b0b0c), c.x + r * 0.4f, c.y + r, false));
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (int i = 0; i < 24; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 24.0f;
        g.drawLine ({ c.getPointOnCircumference (r * 0.86f, a), c.getPointOnCircumference (r * 0.99f, a) }, 1.0f);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2d30), c.x - r * 0.3f, c.y - r * 0.5f, juce::Colour (0xff121314), c.x + r * 0.3f, c.y + r * 0.6f, true));
    g.fillEllipse (c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f);
    const float angle = startAngle + pos * (endAngle - startAngle);
    juce::Path ptr;
    ptr.addRoundedRectangle (-1.6f, -r * 0.98f, 3.2f, r * 0.62f, 1.6f);
    g.setColour (juce::Colours::white);
    g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c));
}

void PadLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (r.withSizeKeepingCentre (r.getWidth(), 4.0f), 2.0f);
    g.setColour (orange);
    g.fillRoundedRectangle (juce::Rectangle<float> (10.0f, std::min (16.0f, r.getHeight())).withCentre ({ pos, r.getCentreY() }), 3.0f);
}

void PadLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const bool lit = b.getToggleState();
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 5.0f);
    g.setColour (lit ? juce::Colour (0xffd32a2a) : juce::Colour (0xff2b2c2f));
    g.fillRoundedRectangle (r, 5.0f);
    if (over || down) { g.setColour (juce::Colours::white.withAlpha (down ? 0.14f : 0.06f)); g.fillRoundedRectangle (r, 5.0f); }
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void PadLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (print.withAlpha (b.isEnabled() ? 0.95f : 0.4f));
    g.setFont (labelFont (11.0f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 0), juce::Justification::centred, 1, 0.7f);
}

//==============================================================================
PadEditor::Help::Help()
{
    static const char* names[] = { "PRINCIPE", "PROGRAMME 1", "PROGRAMME 2", "PROGRAMME 3", "KILL FX", "SETUP LPD8", "SYNCHRO & SON" };
    for (int i = 0; i < 7; ++i)
    {
        auto* b = tabs.add (new juce::TextButton (names[i]));
        b->onClick = [this, i] { show (i); };
        addAndMakeVisible (b);
    }
    close.onClick = [this] { setVisible (false); };
    addAndMakeVisible (close);
    text.setMultiLine (true); text.setReadOnly (true); text.setScrollbarsShown (true); text.setCaretVisible (false);
    text.setFont (juce::Font (juce::FontOptions (15.0f)));
    text.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff111316));
    text.setColour (juce::TextEditor::textColourId, juce::Colour (0xffe8e6df));
    text.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    text.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (text);
}

void PadEditor::Help::show (int tab)
{
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState (i == tab, juce::dontSendNotification);
    text.setText (wikiText (tab), false);
    text.moveCaretToTop (false);
    setVisible (true);
    toFront (false);
}

void PadEditor::Help::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xf4111316));
    g.setColour (orange);
    g.setFont (logoFont (15.0f, 0.15f));
    g.drawText ("WIKI BRUME PAD", getLocalBounds().removeFromTop (44).reduced (14, 0), juce::Justification::centredLeft);
}

void PadEditor::Help::resized()
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
void PadEditor::PadButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const int prog = ed.proc.getProgram();
    auto col = padColour (ed.proc, prog, index);
    const float fl = ed.proc.padFlash (index);
    if (fl > 0.0f) col = col.brighter (fl * 0.8f);
    if (ed.programSelectMode && index < 4) col = index == prog ? juce::Colours::white : juce::Colour (0xff5a2a10);

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 7.0f);
    // RGB light ring through the rubber, like the MK2
    g.setColour (col);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (padRubber);
    g.fillRoundedRectangle (r.reduced (4.0f), 5.0f);
    g.setGradientFill (juce::ColourGradient (col.withAlpha (0.28f), r.getCentreX(), r.getCentreY(), col.withAlpha (0.0f), r.getX(), r.getY(), true));
    g.fillRoundedRectangle (r.reduced (4.0f), 5.0f);

    g.setColour (print);
    g.setFont (labelFont (12.5f));
    juce::String label = ed.programSelectMode && index < 4 ? "PROGRAM " + juce::String (index + 1)
                       : prog == 2 ? "STEP " + juce::String (ed.proc.getPatternPage() * 8 + index + 1)
                       : juce::String (padLabel (prog, index));
    g.drawFittedText (label, r.reduced (8.0f).toNearestInt(), juce::Justification::centred, 2, 0.8f);
}

void PadEditor::PadButton::mouseDown (const juce::MouseEvent&)
{
    if (ed.programSelectMode)
    {
        if (index < 4) ed.proc.selectProgram (index);
        ed.programSelectMode = false;
        ed.progBtn.setToggleState (false, juce::dontSendNotification);
        return;
    }
    ed.proc.padFromScreen (index, true, ed.proc.getProgram());
}

void PadEditor::PadButton::mouseUp (const juce::MouseEvent&)
{
    ed.proc.padFromScreen (index, false, ed.proc.getProgram());
}

//==============================================================================
PadEditor::PadEditor (PadProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (body);
    body.setBounds (0, 0, kW, kH);

    for (int i = 0; i < 8; ++i)
    {
        pads.push_back (std::make_unique<PadButton> (*this, i));
        body.addAndMakeVisible (*pads.back());
    }
    for (int k = 0; k < 8; ++k)
    {
        auto& s = knobs[(size_t) k];
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (0.0, 1.0, 1.0 / 127.0);
        s.setRotaryParameters (juce::degreesToRadians (-140.0f), juce::degreesToRadians (140.0f), true);
        s.setDoubleClickReturnValue (true, 0.0);
        s.onValueChange = [this, k] { proc.knobFromScreen (k, (float) knobs[(size_t) k].getValue(), proc.getProgram()); };
        body.addAndMakeVisible (s);
    }

    progBtn.onClick = [this] { programSelectMode = ! programSelectMode; progBtn.setToggleState (programSelectMode, juce::dontSendNotification); };
    noteBtn.setToggleState (true, juce::dontSendNotification);
    // in program 3 the NOTE / PROG CHNG buttons choose the half of the 16-step pattern (like on the LPD8)
    noteBtn.onClick = [this] { proc.setPatternPage (0); };
    pcBtn.onClick = [this] { proc.setPatternPage (1); };
    targetAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id::fxTarget, targetBtn);
    targetBtn.setClickingTogglesState (true);
    body.addAndMakeVisible (targetBtn);
    ccBtn.onClick = [this] { proc.killFx(); };
    setupBtn.onClick = [this] { if (proc.getLearnStep() > 0) proc.cancelLearn(); else proc.startLearn(); };
    helpBtn.onClick = [this] { help.setBounds (getLocalBounds()); help.show (proc.getProgram() <= 2 ? 1 + proc.getProgram() : 0); };
    for (auto* b : { &progBtn, &noteBtn, &pcBtn, &ccBtn, &setupBtn, &helpBtn }) body.addAndMakeVisible (*b);

    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    body.addAndMakeVisible (bpmSlider);
    bpmAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id::bpm, bpmSlider);
    offsetSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    offsetSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    offsetSlider.setDoubleClickReturnValue (true, 0.0);
    body.addAndMakeVisible (offsetSlider);
    offsetAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id::syncOffset, offsetSlider);
    masterKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    masterKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    masterKnob.setRotaryParameters (juce::degreesToRadians (-140.0f), juce::degreesToRadians (140.0f), true);
    body.addAndMakeVisible (masterKnob);
    masterAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id::master, masterKnob);

    layoutBody();
    addChildComponent (updateBanner);
    addChildComponent (help);
    setResizable (true, true);
    setResizeLimits ((int) (kW * 0.6f), (int) (kH * 0.6f), (int) (kW * 1.6f), (int) (kH * 1.6f));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kW / (double) kH);
    setSize ((int) (kW * 0.9f), (int) (kH * 0.9f));
    startTimerHz (30);
}

PadEditor::~PadEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PadEditor::layoutBody()
{
    for (int i = 0; i < 8; ++i) pads[(size_t) i]->setBounds (padBounds (i));
    for (int k = 0; k < 8; ++k) knobs[(size_t) k].setBounds (knobBounds (k));
    // the four small buttons on the left, as on the MK2 (2 x 2)
    progBtn.setBounds (44, 214, 84, 40);
    noteBtn.setBounds (136, 214, 84, 40);
    pcBtn.setBounds (44, 262, 84, 40);
    ccBtn.setBounds (136, 262, 84, 40);
    targetBtn.setBounds (44, 314, 176, 26);
    // strip under the controller
    setupBtn.setBounds (430, 404, 110, 34);
    offsetSlider.setBounds (556, 408, 130, 24);
    bpmSlider.setBounds (700, 408, 150, 24);
    masterKnob.setBounds (870, 396, 46, 46);
    helpBtn.setBounds (1000, 402, 40, 40);
}

void PadEditor::resized()
{
    body.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kW));
    updateBanner.setBounds (0, 0, getWidth(), 40);
    if (help.isVisible()) help.setBounds (getLocalBounds());
}

void PadEditor::timerCallback()
{
    const int prog = proc.getProgram();
    for (int k = 0; k < 8; ++k)
    {
        auto& s = knobs[(size_t) k];
        if (! s.isMouseButtonDown())
            s.setValue (proc.getKnobValue (prog, k), juce::dontSendNotification);
        s.setEnabled (prog <= 2);
    }
    const bool p3 = prog == 2;
    noteBtn.setEnabled (p3); pcBtn.setEnabled (p3);
    noteBtn.setToggleState (! p3 || proc.getPatternPage() == 0, juce::dontSendNotification);
    pcBtn.setToggleState (p3 && proc.getPatternPage() == 1, juce::dontSendNotification);
    targetBtn.setButtonText (proc.isSoundsOnly() ? "FX : SONS SEULS" : "FX : MUSIQUE + SONS");
    setupBtn.setButtonText (proc.getLearnStep() > 0 ? "CANCEL SETUP" : "SETUP LPD8");
    setupBtn.setToggleState (proc.getLearnStep() > 0, juce::dontSendNotification);
    ccBtn.setToggleState (proc.getKillFlash() > 0.0f, juce::dontSendNotification);
    bpmSlider.setEnabled (proc.getSyncSource() == 0);
    shownProg = prog;
    body.repaint();
}

void PadEditor::paint (juce::Graphics& g) { g.fillAll (desk); }

void PadEditor::paintBody (juce::Graphics& g)
{
    g.fillAll (desk);
    const auto b = kBody;
    const int prog = proc.getProgram();

    // chassis with red end caps
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (b.translated (0.0f, 6.0f), 14.0f);
    g.setColour (trim);
    g.fillRoundedRectangle (b, 14.0f);
    const auto top = b.reduced (9.0f, 0.0f);
    g.setGradientFill (juce::ColourGradient (chassisHi, top.getX(), top.getY(), chassis, top.getX(), top.getBottom(), false));
    g.fillRect (top);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.fillRect (top.withHeight (1.5f));

    // logo block (Brume's own print, no third-party brand)
    g.setColour (print);
    g.setFont (logoFont (26.0f, 0.12f));
    g.drawText ("BRUME", juce::Rectangle<float> (44.0f, 52.0f, 200.0f, 34.0f), juce::Justification::centredLeft);
    g.setColour (trim.brighter (0.2f));
    g.setFont (logoFont (30.0f, 0.06f));
    g.drawText ("PAD", juce::Rectangle<float> (44.0f, 88.0f, 200.0f, 36.0f), juce::Justification::centredLeft);
    g.setColour (printDim);
    g.setFont (labelFont (9.5f, true, 0.18f));
    g.drawText ("X-PAD  -  FX  -  WHITE NOISE", juce::Rectangle<float> (44.0f, 126.0f, 210.0f, 14.0f), juce::Justification::centredLeft);

    // program indicator (what the pads and knobs control now)
    {
        const auto pr = juce::Rectangle<float> (44.0f, 152.0f, 176.0f, 46.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (pr, 6.0f);
        g.setColour (orange);
        g.setFont (logoFont (11.0f, 0.12f));
        g.drawText ("PROGRAM " + juce::String (prog + 1), pr.withHeight (24.0f).translated (0.0f, 2.0f), juce::Justification::centred);
        g.setColour (print);
        g.setFont (labelFont (11.0f));
        static const char* names[] = { "X-PAD + FX", "BRUME", "PATTERN PLAYER", "(EMPTY)" };
        g.drawText (names[juce::jlimit (0, 3, prog)], pr.withTrimmedTop (22.0f), juce::Justification::centred);
    }
    g.setColour (printDim);
    g.setFont (labelFont (8.5f, true, 0.1f));
    g.drawText ("+ PAD 1-4", progBtn.getBounds().toFloat().translated (0.0f, 40.0f).withHeight (11.0f), juce::Justification::centred);
    g.setColour (orange);
    g.drawText ("KILL FX", ccBtn.getBounds().toFloat().translated (0.0f, 40.0f).withHeight (11.0f), juce::Justification::centred);
    if (prog == 2)
    {
        g.setColour (printDim);
        g.drawText ("STEPS 1-8", noteBtn.getBounds().toFloat().translated (0.0f, -12.0f).withHeight (11.0f), juce::Justification::centred);
        g.drawText ("STEPS 9-16", pcBtn.getBounds().toFloat().translated (0.0f, 40.0f).withHeight (11.0f), juce::Justification::centred);
    }

    // pad print: "PAD n" above, "PROG n" in orange for pads 1-4
    for (int i = 0; i < 8; ++i)
    {
        const auto pbn = padBounds (i).toFloat();
        g.setColour (print);
        g.setFont (labelFont (9.5f));
        g.drawText ("PAD " + juce::String (i + 1), pbn.translated (0.0f, -16.0f).withHeight (14.0f), juce::Justification::centredLeft);
        if (i < 4)
        {
            g.setColour (orange);
            g.drawText ("PROG " + juce::String (i + 1), pbn.translated (0.0f, -16.0f).withHeight (14.0f), juce::Justification::centredRight);
        }
    }

    // knob print: K-box, function (changes with the program), value
    for (int k = 0; k < 8; ++k)
    {
        const auto kb = knobBounds (k).toFloat();
        const auto boxR = juce::Rectangle<float> (22.0f, 13.0f).withCentre ({ kb.getCentreX(), kb.getY() - 34.0f });
        g.setColour (print);
        g.drawRoundedRectangle (boxR, 2.0f, 1.0f);
        g.setFont (labelFont (9.0f));
        g.drawText ("K" + juce::String (k + 1), boxR, juce::Justification::centred);
        const bool killed = prog == 0 && proc.isFxKilled (k);
        g.setColour (killed ? juce::Colour (0xffff4040) : orange);
        g.setFont (labelFont (10.0f));
        g.drawFittedText (knobLabel (prog, k), juce::Rectangle<int> ((int) kb.getCentreX() - 40, (int) kb.getY() - 22, 80, 14), juce::Justification::centred, 1, 0.7f);
        g.setColour (printDim);
        g.setFont (labelFont (9.0f, false));
        g.drawText (killed ? juce::String ("KILLED") : proc.getKnobText (prog, k), juce::Rectangle<float> (kb.getCentreX() - 40.0f, kb.getBottom() + 1.0f, 80.0f, 12.0f), juce::Justification::centred);
    }

    // setup assistant
    if (const int step = proc.getLearnStep(); step > 0)
    {
        const auto banner = juce::Rectangle<float> (250.0f, 22.0f, 790.0f, 30.0f);
        g.setColour (orange);
        g.fillRoundedRectangle (banner, 6.0f);
        g.setColour (juce::Colours::black);
        g.setFont (labelFont (13.0f));
        static const char* msgs[] = { "",
            "SETUP 1/6 : sur le LPD8, PROGRAM + PAD 1, puis appuie sur PAD 1",
            "SETUP 2/6 : programme 1, tourne K1",
            "SETUP 3/6 : PROGRAM + PAD 2, puis appuie sur PAD 1",
            "SETUP 4/6 : programme 2, tourne K1",
            "SETUP 5/6 : PROGRAM + PAD 3, puis appuie sur PAD 1 (mode NOTE)",
            "SETUP 6/6 : programme 3, tourne K1" };
        g.drawText (juce::String::fromUTF8 (msgs[juce::jlimit (0, 6, step)]), banner, juce::Justification::centred);
    }

    // KILL flash
    if (const float kf = proc.getKillFlash(); kf > 0.0f)
    {
        g.setColour (juce::Colour (0xffff3030).withAlpha (0.35f * kf));
        g.drawRoundedRectangle (b.reduced (2.0f), 14.0f, 6.0f);
    }

    // strip under the controller
    {
        g.setColour (juce::Colour (0xff17181b));
        g.fillRoundedRectangle (juce::Rectangle<float> (14.0f, 360.0f, 1032.0f, 100.0f), 10.0f);
        const int src = proc.getSyncSource();
        g.setFont (logoFont (12.0f, 0.08f));
        g.setColour (src == 1 || src == 3 ? juce::Colour (0xff4cd964) : orange);
        const juce::String srcName = src == 1 ? "SYNC MIDI CLOCK  " : src == 3 ? "SYNC LINK  " : src == 2 ? "SYNC HOST  " : "INTERNAL  ";
        g.drawText (srcName + juce::String (proc.getBpm(), 1) + " BPM",
                    juce::Rectangle<float> (32.0f, 376.0f, 300.0f, 20.0f), juce::Justification::centredLeft);
        // beat dots
        const int beatNow = (int) proc.getBarBeat();
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (i == beatNow ? (i == 0 ? juce::Colour (0xffff3030) : orange) : juce::Colour (0xff2e3034));
            g.fillEllipse (330.0f + 18.0f * (float) i, 380.0f, 10.0f, 10.0f);
        }
        g.setFont (labelFont (11.5f, false));
        g.setColour (print.withAlpha (0.8f));
        const int peers = proc.getLinkPeers();
        g.drawText (proc.getMidiStatus() + "   LINK " + (peers > 0 ? juce::String (peers) + " PEER" + (peers > 1 ? "S" : "") : juce::String ("WAITING")),
                    juce::Rectangle<float> (32.0f, 402.0f, 390.0f, 18.0f), juce::Justification::centredLeft);
        g.setColour (printDim);
        const auto ev = proc.getLastEvent();
        g.drawText ((ev.isNotEmpty() ? ev + "    " : juce::String()) + "V" + Updater::currentVersion(),
                    juce::Rectangle<float> (32.0f, 426.0f, 390.0f, 18.0f), juce::Justification::centredLeft);
        g.setFont (labelFont (9.0f, true, 0.1f));
        g.drawText ("TEMPO " + juce::String (proc.apvts.getRawParameterValue (id::bpm)->load(), 1) + " (NO SYNC)", juce::Rectangle<float> (700.0f, 434.0f, 150.0f, 12.0f), juce::Justification::centred);
        g.drawText ("MASTER", juce::Rectangle<float> (853.0f, 442.0f, 80.0f, 12.0f), juce::Justification::centred);
        g.drawText ("SYNC OFFSET " + proc.apvts.getParameter (id::syncOffset)->getCurrentValueAsText(), juce::Rectangle<float> (546.0f, 434.0f, 150.0f, 12.0f), juce::Justification::centred);
        g.drawText ("WIKI", juce::Rectangle<float> (990.0f, 444.0f, 60.0f, 12.0f), juce::Justification::centred);
    }
}
