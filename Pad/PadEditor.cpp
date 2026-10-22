#include "PadEditor.h"
#include "BinaryData.h"

using namespace bpad;

namespace
{
    constexpr int kW = PadEditor::baseW, kH = PadEditor::baseH;
    // "Graphite" look: matte dark body, one orange accent, flat arc knobs, segmented meter
    const juce::Colour desk { 0xff0b0c0e }, bodyTop { 0xff1c1e22 }, bodyBot { 0xff15161a }, edge { 0xff2a2d33 },
                       panel { 0xff131417 }, panelEdge { 0xff24262b }, well { 0xff0f1013 }, wellEdge { 0xff262930 },
                       print { 0xffe9eaee }, printDim { 0xff8a8e97 }, printFaint { 0xff5f636b }, orange { 0xffff7a1a },
                       blue { 0xff3aa8ff }, green { 0xff3ddc84 }, red { 0xffff5a5a }, track { 0xff2a2d33 }, capFill { 0xff1f2125 }, capEdge { 0xff34373e };

    juce::Typeface::Ptr typeface (const char* data, int size) { return juce::Typeface::createSystemTypefaceFor (data, (size_t) size); }
    juce::Typeface::Ptr interSemi() { static auto t = typeface (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize); return t; }
    juce::Typeface::Ptr interBold() { static auto t = typeface (BinaryData::InterBold_ttf, BinaryData::InterBold_ttfSize); return t; }
    juce::Font logoFont (float h, float kerning = 0.08f)
    {
        static auto tf = typeface (BinaryData::SyncopateBold_ttf, BinaryData::SyncopateBold_ttfSize);
        juce::Font f (juce::FontOptions (tf).withHeight (h));
        f.setExtraKerningFactor (kerning);
        return f;
    }
    juce::Font labelFont (float h, bool bold = true, float kerning = 0.06f)
    {
        juce::Font f (juce::FontOptions (bold ? interBold() : interSemi()).withHeight (h));
        f.setExtraKerningFactor (kerning);
        return f;
    }
    juce::Font monoFont (float h)
    {
        static auto tf = typeface (BinaryData::JetBrainsMonoMedium_ttf, BinaryData::JetBrainsMonoMedium_ttfSize);
        return juce::Font (juce::FontOptions (tf).withHeight (h));
    }

    // geometry (base coordinates)
    const juce::Rectangle<float> kBody { 14.0f, 14.0f, 1032.0f, 330.0f };
    constexpr float kPanelX = 1058.0f;
    constexpr int padSize = 90, padGap = 12, padX0 = 262, padTopY = 62, padBotY = 202;
    constexpr int knobSize = 62, knobColW = 88, knobX0 = 674, knobTopY = 80, knobBotY = 222;

    juce::Rectangle<int> padBounds (int index)   // index 0..7 = PAD 1..8 (1-4 bottom row, 5-8 top row)
    {
        const int col = index % 4, y = index < 4 ? padBotY : padTopY;
        return { padX0 + col * (padSize + padGap), y, padSize, padSize };
    }
    juce::Rectangle<int> knobBounds (int k)      // K1-K4 top, K5-K8 bottom
    {
        const int col = k % 4, y = k < 4 ? knobTopY : knobBotY;
        return { knobX0 + col * knobColW + (knobColW - knobSize) / 2, y, knobSize, knobSize };
    }

    struct PadLook { juce::Colour colour; bool lit; };
    PadLook padLook (const PadProcessor& p, int prog, int pad)
    {
        const float bb = p.getBarBeat();
        const bool onBeat = bb - std::floor (bb) < 0.2f;
        const bool down = p.padIsDown (pad);
        if (prog == 0)
        {
            if (pad >= 4)
            {
                // one colour per sound: KICK red, SNARE orange, CLAP yellow, HAT green
                static const juce::Colour soundCol[] = { juce::Colour (0xffff4d4d), juce::Colour (0xffff8a1f), juce::Colour (0xffffd23f), juce::Colour (0xff3ddc84) };
                const auto c = soundCol[pad - 4];
                const bool on = p.getActiveSound() == pad - 4;
                return { on && onBeat ? c.brighter (0.35f) : c, on };
            }
            return { p.getActiveSound() >= 0 && onBeat && p.getActiveSpeed() == pad ? blue.brighter (0.4f) : blue, p.getActiveSpeed() == pad };
        }
        if (prog == 1)
        {
            switch (pad)
            {
                case 4: return { red, p.isBrumeOn() };
                case 5: return { green, onBeat };
                case 7: return { print, down };
                default: return { edge, false };   // bottom row unused in this program
            }
        }
        if (prog == 2)
        {
            const int step = p.getPatternPage() * 8 + stepOfPad (pad);
            const bool on = (p.getPatternSteps() >> step) & 1;
            if (p.getPatternPlayStep() == step) return { print, true };
            return { step % 4 == 0 ? red : orange, on };
        }
        if (prog == 3)
        {
            if (pad == 4) return { print, down };                 // VINYL BRAKE while held
            if (pad == 5) return { orange, p.fxInputOn() };
            if (pad == 6) return { orange, p.fxHydraOn() };
        }
        return { edge, false };
    }

    juce::String wikiText (int tab)
    {
        auto u = [] (const char* s) { return juce::String::fromUTF8 (s); };
        switch (tab)
        {
            case 0: return u (
"LE PRINCIPE\n\n"
"Hydra transforme un Akai LPD8 MK2 en boîte d'effets façon RMX-1000 / DJM-V10 pour Traktor.\n\n"
"- La musique de Traktor passe dans Hydra (Traktor -> BlackHole -> Hydra -> enceintes) : les échos, filtres "
"et réverbes s'appliquent au morceau, comme un RMX-1000 branché entre la table et les enceintes.\n"
"- Le LPD8 a 4 PROGRAMMES. Sur le contrôleur : maintenir PROGRAM et appuyer sur PAD 1, 2, 3 ou 4.\n"
"     Programme 1 = X-PAD (kick, snare, clap, hi-hat en roulements) + effets Send du DJM-V10\n"
"     Programme 2 = WHITE NOISE (bruit blanc façon Revolo, réverbe, trémolo)\n"
"     Programme 3 = PATTERN PLAYER (boîte à rythme 16 pas, comme dans Traktor)\n"
"     Programme 4 = V10 FX (delay, spiral, dub echo, shimmer, vinyl brake)\n"
"  Hydra reconnaît tout seul le programme utilisé (après le SETUP LPD8) et l'écran change ses étiquettes.\n"
"- À l'écran : cliquer sur PROGRAM puis sur un des pads 1 à 4, ou utiliser les pads et boutons à la souris.\n"
"- Tout est calé sur le tempo de Traktor grâce à l'horloge MIDI (onglet SYNCHRO).\n"
"- KILL FX coupe tous les effets d'un coup en laissant les queues d'écho et de réverbe s'éteindre.\n"
"- FX SOURCE (panneau de droite, comme sur le RMX-1000) : ce qui entre dans les effets.\n"
"     INPUT = la musique de Traktor. HYDRA = les sons d'Hydra (roulements X-PAD, Pattern Player).\n"
"     Les deux allumés : tout passe dans les effets. Une source éteinte passe intacte, sans effet.\n"
"     Le bruit blanc passe toujours dans les effets (comme le NOISE du RMX-1000).\n"
"     Sur le LPD8 : PAD 6 + PAD 7 en même temps allume / éteint INPUT ; programme 4 : PAD 6 = INPUT, PAD 7 = HYDRA.");
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
"   K3 LPF : filtre passe-bas. À zéro il ne fait rien, en tournant le son s'assombrit jusqu'à 150 Hz.\n\n"
"   EFFETS SEND FAÇON DJM-V10 (K4, K7, K8 = quantité de musique envoyée dans chaque effet ; les autres effets V10 : programme 4) :\n"
"   K4 LONG DELAY : delay long.\n"
"   K7 DUB ECHO : écho sombre qui se fond en réverbe, comme sur la V10 / 900NXS2.\n"
"   K8 REVERB : réverbe plate (algorithme Dattorro), dense et douce.\n"
"   K5 SIZE / FEEDBACK : pour les delays et le dub echo, la durée des répétitions (à fond = elles continuent "
"jusqu'au KILL FX) ; pour la réverbe, la taille (pré-délai et densité), sans jamais déformer le son.\n"
"   K6 TIME : la durée, en continu. Long delay de 1/4 à 1 temps, dub echo de 1/8 à 1 temps : tourner TIME "
"pendant que les répétitions sonnent les fait glisser en hauteur, comme une bande (DJM-V10). Réverbe de 0,8 à 10 s. "
"Tout est calé sur le tempo de Traktor.\n\n"
"Baisser un envoi (K4, K7, K8) laisse la queue sonner. KILL FX ouvre le LPF, ferme les envois et réduit le feedback pour que les "
"queues s'éteignent toutes seules.\n\n"
"EXEMPLE DE DROP : SNARE (le roulement part en 1/4), puis 1/8, 1/16, 1/32 en montant K1 et K2 ; au drop, "
"ré-appuyer sur SNARE (ou baisser K1).");
            case 2: return u (
"PROGRAMME 2 - WHITE NOISE\n\n"
"Le bruit blanc est voicé comme le Teile Revolo (mesuré sur tes vidéos : bruit large de 170 Hz à 10 kHz, "
"gauche et droite indépendants, qui s'épaissit dans la réverbe).\n\n"
"PADS\n"
"   PAD 5 ON / OFF : allume / éteint le bruit blanc (la queue s'éteint selon LENGTH)\n"
"   PAD 6 TAP : taper la vitesse du trémolo (en rythme avec la musique)\n"
"   PAD 8 HOLD : bruit blanc allumé tant qu'on appuie\n"
"   PADS 1 à 4 (rangée du bas) : inactifs dans ce programme. La vitesse du trémolo se règle en Hz avec K7 TEMPO.\n\n"
"BOUTONS\n"
"   K1 REVERB (quantité de musique envoyée dans la réverbe) · K2 CHARACTER · K3 LENGTH · K4 AIR\n"
"   K5 NOISE (volume du bruit) · K6 TREMOLO · K7 TEMPO (vitesse du trémolo en Hz, sans palier ni coupure) · K8 FILTER\n\n"
"Baisser NOISE fait comme OFF : le bruit s'efface au rythme de la queue de réverbe (réglée par LENGTH).");
            case 3: return u (
"PROGRAMME 3 - PATTERN PLAYER\n\n"
"Une boîte à rythme de 16 pas (une mesure en doubles-croches), calée sur le tempo et qui repart toujours sur le 1er temps, "
"comme le Pattern Player de Traktor.\n\n"
"LES 16 PAS AVEC 8 PADS\n"
"   Les pas se lisent comme une page : rangée du haut puis rangée du bas.\n"
"   Mode NOTE (bouton NOTE du LPD8) : PAD 5 à 8 = pas 1 à 4, PAD 1 à 4 = pas 5 à 8 (1re moitié de la mesure).\n"
"   Mode PROG CHNG (bouton PROG CHNG du LPD8) : les mêmes pads = pas 9 à 12 (haut) et 13 à 16 (bas).\n"
"   Un appui allume / éteint le pas. Le pas en cours de lecture clignote en blanc ; les pas rouges tombent sur les temps.\n"
"   À l'écran : NOTE / PROG CHNG choisissent la moitié affichée, puis cliquer sur les pads.\n\n"
"BOUTONS\n"
"   K1 VOLUME : à zéro, rien ne joue. C'est le bouton pour lancer / couper le pattern.\n"
"   K2 PITCH : hauteur du son (-12 à +12).\n"
"   K3 DECAY : longueur du son (court = sec, à fond = son entier).\n"
"   K4 DUCK : baisse la musique à chaque coup (effet pompe). Seulement quand FX SOURCE INPUT est allumé.\n"
"   K5 KIT : 808, 909, RMX (les sons extraits de ton RMX-1000) ou USER (tes propres sons : bouton SAMPLES, "
"panneau de droite ; WAV, AIFF, FLAC, MP3).\n"
"   K6 SOUND : KICK, SNARE, CLAP, CL HAT, OP HAT, RIM, TOM, CYMBAL.\n"
"   K7 PATTERN : 8 patterns de départ (4 temps, contretemps, backbeat, croches, doubles, syncopé, shuffle, fill). "
"Tourner K7 charge le pattern ; tes modifications de pas restent tant que tu ne le tournes pas.\n"
"   K8 SWING : retarde les pas pairs (groove shuffle).\n\n"
"ASTUCE : choisir CL HAT + PATTERN 2 (contretemps) pour ajouter un charley sur un morceau, ou CLAP + PATTERN 3 avant un drop.");
            case 4: return u (
"KILL FX\n\n"
"Coupe tout d'un coup : LPF (rouvert), short delay, long delay, dub echo, réverbe, white noise, et remet tous les pads à zéro : X-PAD (son et vitesse éteints), White noise (ON éteint), Pattern Player (tous les pas éteints ; ré-appuyer sur des pas ou tourner K7 PATTERN pour repartir). Les échos et réverbes "
"gardent leur queue (le feedback est réduit pour qu'elle s'éteigne), rien ne s'arrête brutalement.\n\n"
"TROIS FAÇONS DE LE DÉCLENCHER\n"
"   1. Sur le LPD8 : appuyer sur CC puis sur n'importe quel pad (en mode CC les pads envoient des messages CC, "
"que Hydra interprète comme KILL FX). Appuyer ensuite sur NOTE pour revenir aux pads normaux.\n"
"   2. Sur le LPD8 : appuyer en même temps sur PAD 5 et PAD 8.\n"
"   3. À l'écran : le bouton CC · KILL.\n\n"
"AUTRE COMBINAISON : PAD 6 + PAD 7 en même temps allume / éteint FX SOURCE INPUT, dans tous les programmes.\n\n"
"Le bouton CC seul n'envoie rien (c'est un changement de mode du contrôleur), d'où la combinaison CC + pad.\n\n"
"Après un KILL, un effet reste coupé même si son bouton est encore tourné : il revient dès que tu bouges ce bouton.");
            case 6: return u (
"PROGRAMME 4 - V10 FX\n\n"
"Les effets du DJM-V10, un effet par colonne de boutons : en haut le temps (ou la taille), en bas la quantité.\n\n"
"   K1 DELAY TIME / K5 DELAY : delay propre, de 1/8 de temps à 2 temps. Tourner le temps pendant que les répétitions sonnent "
"les fait glisser en hauteur, comme sur la V10.\n"
"   K2 SPIRAL TIME / K6 SPIRAL : écho dont les répétitions se fondent en réverbe (Spiral de la V10), 1/8 à 1 temps.\n"
"   K3 DUB TIME / K7 DUB ECHO : écho à bande, sombre et chaud, 1/8 à 1 temps.\n"
"   K4 SHIMMER SIZE / K8 SHIMMER : réverbe dont la queue monte d'une octave (son aérien).\n\n"
"PADS\n"
"   PAD 5 VINYL BRAKE (maintenir) : tout le son ralentit jusqu'à l'arrêt en un temps, comme un vinyle qu'on stoppe ; "
"relâcher repart aussitôt.\n"
"   PAD 6 / PAD 7 : FX SOURCE INPUT / HYDRA marche / arrêt.\n\n"
"Les effets sont des envois : baisser la quantité laisse la queue s'éteindre. KILL FX les coupe aussi.\n"
"Le MASTER se règle dans le panneau de droite.");
            case 5: return u (
"SETUP LPD8 (une seule fois)\n\n"
"Chaque programme du LPD8 envoie ses propres numéros de notes et de boutons. Le SETUP les apprend, ensuite "
"Hydra sait toujours sur quel programme tu es.\n\n"
"   1. Cliquer sur SETUP LPD8.\n"
"   2. Sur le LPD8 : PROGRAM + PAD 1 (programme 1), puis appuyer sur PAD 1, puis tourner K1.\n"
"   3. PROGRAM + PAD 2 (programme 2), puis appuyer sur PAD 1, puis tourner K1.\n"
"   4. PROGRAM + PAD 3 (programme 3), puis appuyer sur PAD 1, puis tourner K1.\n"
"   5. PROGRAM + PAD 4 (programme 4), puis appuyer sur PAD 1, puis tourner K1.\n"
"   6. Pour le Pattern Player : PROGRAM + PAD 3, PROG CHNG et PAD 1 (pour que les pas 9 à 16 tombent sur les bons pads), "
"puis revenir en NOTE. Ou SKIP si inutile.\n"
"C'est tout. Le LPD8 doit être en mode NOTE (bouton NOTE).\n\n"
"Sans SETUP, Hydra fonctionne quand même avec les réglages d'usine du LPD8 MK2, et le programme se choisit à l'écran.\n\n"
"Le LPD8 et le port d'horloge de Traktor sont ouverts automatiquement par l'app (rien à cocher).");
            default: return u (
"SYNCHRO TRAKTOR\n"
"   LE PLUS SIMPLE : ABLETON LINK. Dans Traktor, appuyer sur LINK (en haut de l'écran). C'est tout : Hydra "
"rejoint la session Link tout seul (en bas : SYNC LINK et LINK 1 PEER). Le Mac peut demander d'autoriser Hydra "
"sur le réseau local : accepter.\n\n"
"   OU L'HORLOGE MIDI :\n"
"   1. Traktor > Préférences > Controller Manager > Add... > Generic MIDI, Out-Port : Traktor Virtual Output.\n"
"   2. Traktor > Préférences > External Sync : cocher Enable MIDI Clock.\n"
"   3. Dans Traktor, panneau Master Clock (métronome) : lancer l'horloge.\n"
"   Hydra ouvre Traktor Virtual Output tout seul. En bas : SYNC MIDI CLOCK et le tempo.\n\n"
"   CALAGE FIN : si les roulements arrivent un peu avant ou après le temps de la musique, régler SYNC OFFSET "
"en bas (+ = roulements plus tard, - = plus tôt ; double-clic = 0). Ne pas décaler le morceau dans Traktor.\n\n"
"SON (la musique passe dans Hydra)\n"
"   - Traktor > Préférences > Audio Setup : un périphérique agrégé (carte son ou enceintes du Mac + BlackHole 2ch), "
"Output Routing : Master sur les canaux de BlackHole (les casques restent sur ta carte son).\n"
"   - Hydra > Options > Audio/MIDI Settings : Input = BlackHole 2ch, Output = ta carte son ou les enceintes, "
"même fréquence (44100 Hz) que Traktor.\n"
"   - Autoriser le micro pour Hydra (Réglages Système > Confidentialité > Microphone) sinon l'entrée reste muette.");
        }
    }
}

//==============================================================================
PadLookAndFeel::PadLookAndFeel()
{
    // also styles the app's own windows (Audio/MIDI settings, menus)
    setColourScheme ({ panel, desk, bodyTop, edge, print, orange, juce::Colours::black, well, print });
    setColour (juce::ResizableWindow::backgroundColourId, panel);
    setColour (juce::DocumentWindow::textColourId, print);
    setColour (juce::ComboBox::backgroundColourId, well);
    setColour (juce::ComboBox::outlineColourId, wellEdge);
    setColour (juce::ComboBox::arrowColourId, orange);
    setColour (juce::PopupMenu::backgroundColourId, bodyTop);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, orange);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::TextButton::buttonColourId, bodyTop);
    setColour (juce::TextButton::buttonOnColourId, orange);
    setColour (juce::ToggleButton::tickColourId, orange);
    setColour (juce::ListBox::backgroundColourId, well);
    setColour (juce::Slider::thumbColourId, orange);
    setColour (juce::Slider::trackColourId, track);
    setColour (juce::Label::textColourId, print);
}

juce::Typeface::Ptr PadLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return f.isBold() ? interBold() : interSemi();
    return juce::LookAndFeel_V4::getTypefaceForFont (f);
}

void PadLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider& s)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float size = std::min (area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float r = size * 0.5f - 3.0f, stroke = std::max (3.0f, size * 0.065f);
    const juce::Colour arc = s.getProperties().contains ("arc") ? juce::Colour ((juce::uint32) (juce::int64) s.getProperties()["arc"]) : orange;
    juce::Path bg, val;
    bg.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
    g.setColour (track);
    g.strokePath (bg, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float angle = startAngle + pos * (endAngle - startAngle);
    if (pos > 0.002f)
    {
        val.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, angle, true);
        g.setColour (arc.withAlpha (0.25f));
        g.strokePath (val, juce::PathStrokeType (stroke * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (arc);
        g.strokePath (val, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const float cr = r * 0.66f;
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - cr, c.y - cr + 2.0f, cr * 2, cr * 2);
    g.setGradientFill (juce::ColourGradient (capFill.brighter (0.15f), c.x, c.y - cr, capFill.darker (0.3f), c.x, c.y + cr, false));
    g.fillEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2);
    g.setColour (capEdge);
    g.drawEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2, 1.0f);
    g.setColour (print);
    g.drawLine ({ c.getPointOnCircumference (cr * 0.2f, angle), c.getPointOnCircumference (cr * 0.85f, angle) }, 2.4f);
}

void PadLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto tr = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
    g.setColour (track);
    g.fillRoundedRectangle (tr, 2.0f);
    g.setColour (s.isEnabled() ? print : juce::Colour (0xff4a4d55));
    g.fillRoundedRectangle (juce::Rectangle<float> (10.0f, std::min (15.0f, r.getHeight())).withCentre ({ pos, r.getCentreY() }), 3.0f);
}

void PadLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool lit = b.getToggleState();
    const bool kill = b.getComponentID() == "kill";
    const auto accent = kill ? red : orange;
    g.setColour (lit ? accent : juce::Colour (0xff1b1d21));
    g.fillRoundedRectangle (r, 8.0f);
    if (over || down) { g.setColour (juce::Colours::white.withAlpha (down ? 0.12f : 0.05f)); g.fillRoundedRectangle (r, 8.0f); }
    g.setColour (lit ? accent : (kill ? juce::Colour (0xff5a2a2a) : juce::Colour (0xff2c2f35)));
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
}

void PadLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool lit = b.getToggleState();
    const bool kill = b.getComponentID() == "kill";
    juce::Colour c = lit ? juce::Colour (0xff16110c) : (kill ? red : juce::Colour (0xffc9ccd2));
    if (! b.isEnabled() && ! lit) c = printFaint;
    g.setColour (c);
    g.setFont (labelFont (10.5f, true, 0.1f));
    g.drawFittedText (b.getButtonText().toUpperCase(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 2, 0.7f);
}

//==============================================================================
PadEditor::Help::Help()
{
    static const char* names[] = { "PRINCIPE", "PROGRAMME 1", "PROGRAMME 2", "PROGRAMME 3", "PROGRAMME 4", "KILL FX", "SETUP LPD8", "SYNCHRO & SON" };
    static const int page[] = { 0, 1, 2, 3, 6, 4, 5, 7 };
    for (int i = 0; i < 8; ++i)
    {
        auto* b = tabs.add (new juce::TextButton (names[i]));
        b->onClick = [this, i] { show (page[i]); };
        addAndMakeVisible (b);
    }
    close.onClick = [this] { setVisible (false); };
    addAndMakeVisible (close);
    text.setMultiLine (true); text.setReadOnly (true); text.setScrollbarsShown (true); text.setCaretVisible (false);
    text.setFont (labelFont (14.5f, false, 0.0f));
    text.setColour (juce::TextEditor::backgroundColourId, well);
    text.setColour (juce::TextEditor::textColourId, print);
    text.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    text.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (text);
}

void PadEditor::Help::show (int tab)
{
    static const int page[] = { 0, 1, 2, 3, 6, 4, 5, 7 };
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState (page[i] == tab, juce::dontSendNotification);
    text.setText (wikiText (tab), false);
    text.moveCaretToTop (false);
    setVisible (true);
    toFront (false);
}

void PadEditor::Help::paint (juce::Graphics& g)
{
    g.fillAll (panel.withAlpha (0.98f));
    auto top = getLocalBounds().removeFromTop (44).reduced (16, 0);
    g.setColour (print);
    g.setFont (logoFont (16.0f));
    g.drawText ("HYDRA", top, juce::Justification::centredLeft);
    g.setColour (orange);
    g.setFont (labelFont (10.0f, true, 0.3f));
    g.drawText ("WIKI", top.withTrimmedLeft (120), juce::Justification::centredLeft);
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
PadEditor::UserKit::UserKit (PadEditor& e) : ed (e)
{
    for (int i = 0; i < 8; ++i)
    {
        auto* c = choose.add (new juce::TextButton ("CHOISIR"));
        c->onClick = [this, i] { pick (i); };
        addAndMakeVisible (c);
        auto* x = clear.add (new juce::TextButton ("RETIRER"));
        x->setComponentID ("kill");
        x->onClick = [this, i] { ed.proc.clearUserSample (i); repaint(); };
        addAndMakeVisible (x);
    }
    close.onClick = [this] { setVisible (false); };
    addAndMakeVisible (close);
}

void PadEditor::UserKit::pick (int slot)
{
    chooser = std::make_unique<juce::FileChooser> ("Sample pour " + juce::String (pad::pp::soundName (slot)),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, slot] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile() && ! ed.proc.loadUserSample (slot, f))
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Hydra",
                                                                          juce::String::fromUTF8 ("Impossible de lire ce fichier audio."));
                              repaint();
                          });
}

void PadEditor::UserKit::paint (juce::Graphics& g)
{
    g.fillAll (panel.withAlpha (0.98f));
    auto top = getLocalBounds().removeFromTop (44).reduced (16, 0);
    g.setColour (print);
    g.setFont (logoFont (16.0f));
    g.drawText ("HYDRA", top, juce::Justification::centredLeft);
    g.setColour (orange);
    g.setFont (labelFont (10.0f, true, 0.3f));
    g.drawText ("USER KIT  -  PATTERN PLAYER", top.withTrimmedLeft (120), juce::Justification::centredLeft);
    g.setColour (printDim);
    g.setFont (labelFont (11.0f, false, 0.02f));
    g.drawText (juce::String::fromUTF8 ("Tes propres sons pour le Pattern Player. Programme 3 : K5 KIT sur USER. Un emplacement vide joue le son du kit RMX."),
                getLocalBounds().reduced (18, 0).withTop (42).withHeight (18), juce::Justification::centredLeft);
    for (int i = 0; i < 8; ++i)
    {
        const int step = juce::jmax (24, (getHeight() - 76) / 8);
        const auto row = juce::Rectangle<int> (18, 68 + i * step, getWidth() - 36, step - 4);
        g.setColour (well);
        g.fillRoundedRectangle (row.toFloat(), 8.0f);
        g.setColour (orange);
        g.setFont (labelFont (11.0f, true, 0.1f));
        g.drawText (pad::pp::soundName (i), row.withWidth (110).withTrimmedLeft (12), juce::Justification::centredLeft);
        const auto path = ed.proc.getUserSamplePath (i);
        g.setColour (path.isEmpty() ? printFaint : print);
        g.setFont (monoFont (11.0f));
        g.drawFittedText (path.isEmpty() ? juce::String::fromUTF8 ("(son du kit RMX)") : juce::File (path).getFileName(),
                          row.withTrimmedLeft (130).withTrimmedRight (230), juce::Justification::centredLeft, 1, 0.7f);
    }
}

void PadEditor::UserKit::resized()
{
    close.setBounds (getWidth() - 44, 6, 36, 32);
    for (int i = 0; i < 8; ++i)
    {
        const int step = juce::jmax (24, (getHeight() - 76) / 8), y = 68 + i * step;
        choose[i]->setBounds (getWidth() - 236, y + 1, 104, step - 6);
        clear[i]->setBounds (getWidth() - 126, y + 1, 104, step - 6);
    }
}

//==============================================================================
void PadEditor::PadButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    const int prog = ed.proc.getProgram();
    auto look = padLook (ed.proc, prog, index);
    const bool select = ed.programSelectMode && index < 4;
    if (select) look = { orange, index == prog };
    const float fl = ed.proc.padFlash (index);
    const float radius = 12.0f;

    if (look.lit)
    {
        // soft glow, coloured rim, light from the centre
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (look.colour.withAlpha (0.07f * (float) (4 - i)));
            g.fillRoundedRectangle (r.expanded ((float) i * 2.0f), radius + (float) i * 2.0f);
        }
        g.setGradientFill (juce::ColourGradient (look.colour.withAlpha (0.32f), r.getCentreX(), r.getY() + r.getHeight() * 0.4f,
                                                 juce::Colour (0xff141518), r.getX(), r.getBottom(), true));
        g.fillRoundedRectangle (r, radius);
        g.setColour (look.colour);
        g.drawRoundedRectangle (r.reduced (1.0f), radius, 2.0f);
    }
    else
    {
        g.setColour (juce::Colour (0xff111215));
        g.fillRoundedRectangle (r, radius);
        g.setColour (look.colour == edge ? edge : look.colour.withAlpha (0.28f));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }
    if (fl > 0.0f) { g.setColour (juce::Colours::white.withAlpha (0.18f * fl)); g.fillRoundedRectangle (r, radius); }

    if (select)
    {
        g.setColour (look.lit ? print : orange);
        g.setFont (labelFont (9.5f, true, 0.16f));
        g.drawText ("PROGRAM " + juce::String (index + 1), r.withTrimmedBottom (r.getHeight() * 0.5f).withTrimmedTop (16.0f), juce::Justification::centred);
        g.setColour (print);
        g.setFont (labelFont (12.0f, true, 0.06f));
        g.drawFittedText (programName (index), r.withTrimmedTop (r.getHeight() * 0.5f).reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centredTop, 2, 0.8f);
        return;
    }
    g.setColour (look.lit ? juce::Colours::white : juce::Colour (0xffb9bcc3));
    if (prog == 2)
    {
        g.setFont (labelFont (28.0f, true, 0.0f));
        g.drawText (juce::String (ed.proc.getPatternPage() * 8 + stepOfPad (index) + 1), r, juce::Justification::centred);
        return;
    }
    g.setFont (labelFont (12.0f, true, 0.1f));
    g.drawFittedText (juce::String (padLabel (prog, index)).toUpperCase(), r.reduced (8.0f).toNearestInt(), juce::Justification::centred, 2, 0.8f);
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
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);   // the settings window gets the same look
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
    auto toggle = [this] (const char* pid) { if (auto* prm = proc.apvts.getParameter (pid)) prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f); };
    fxInputBtn.onClick = [toggle] { toggle (id::fxInput); };
    fxHydraBtn.onClick = [toggle] { toggle (id::fxHydra); };
    body.addAndMakeVisible (fxInputBtn);
    body.addAndMakeVisible (fxHydraBtn);
    ccBtn.setButtonText (juce::String::fromUTF8 ("CC · KILL"));
    ccBtn.setComponentID ("kill");
    ccBtn.onClick = [this] { proc.killFx(); };
    setupBtn.onClick = [this] { if (proc.getLearnStep() > 0) proc.cancelLearn(); else proc.startLearn(); };
    helpBtn.onClick = [this] { help.setBounds (getLocalBounds()); { static const int page[] = { 1, 2, 3, 6 }; help.show (page[juce::jlimit (0, 3, proc.getProgram())]); } };
    samplesBtn.onClick = [this] { openUserKit(); };
    for (auto* b : { &progBtn, &noteBtn, &pcBtn, &ccBtn, &setupBtn, &helpBtn, &samplesBtn }) body.addAndMakeVisible (*b);

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
    masterKnob.setDoubleClickReturnValue (true, std::sqrt (1.0 / 1.25));   // double-click = 0 dB
    body.addAndMakeVisible (masterKnob);
    masterAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id::master, masterKnob);

    layoutBody();
    addChildComponent (updateBanner);
    addChildComponent (help);
    addChildComponent (userKit);
    setResizable (true, true);
    setResizeLimits ((int) (kW * 0.6f), (int) (kH * 0.6f), (int) (kW * 1.6f), (int) (kH * 1.6f));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kW / (double) kH);
    setSize ((int) (kW * 0.9f), (int) (kH * 0.9f));
    startTimerHz (30);
}

PadEditor::~PadEditor()
{
    stopTimer();
    if (&juce::LookAndFeel::getDefaultLookAndFeel() == &lnf) juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void PadEditor::layoutBody()
{
    for (int i = 0; i < 8; ++i) pads[(size_t) i]->setBounds (padBounds (i));
    for (int k = 0; k < 8; ++k) knobs[(size_t) k].setBounds (knobBounds (k));
    // the four mode buttons on the left, as on the MK2 (2 x 2)
    progBtn.setBounds (42, 206, 90, 46);
    noteBtn.setBounds (142, 206, 90, 46);
    pcBtn.setBounds (42, 262, 90, 46);
    ccBtn.setBounds (142, 262, 90, 46);
    // side panel
    const int px = (int) kPanelX;
    offsetSlider.setBounds (px + 16, 112, 126, 20);
    bpmSlider.setBounds (px + 158, 112, 126, 20);
    masterKnob.setBounds (px + 16, 156, 66, 66);
    fxInputBtn.setBounds (px + 146, 174, 138, 32);
    fxHydraBtn.setBounds (px + 146, 212, 138, 32);
    setupBtn.setBounds (px + 14, 290, 116, 40);
    samplesBtn.setBounds (px + 136, 290, 86, 40);
    helpBtn.setBounds (px + 228, 290, 58, 40);
}

void PadEditor::resized()
{
    body.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kW));
    updateBanner.setBounds (0, 0, getWidth(), 40);
    if (help.isVisible()) help.setBounds (getLocalBounds());
    if (userKit.isVisible()) userKit.setBounds (getLocalBounds());
}

void PadEditor::timerCallback()
{
    const int prog = proc.getProgram();
    for (int k = 0; k < 8; ++k)
    {
        auto& s = knobs[(size_t) k];
        if (! s.isMouseButtonDown())
            s.setValue (proc.getKnobValue (prog, k), juce::dontSendNotification);
        const bool used = juce::String (knobLabel (prog, k)).isNotEmpty();
        s.setEnabled (used);
        s.setAlpha (used ? 1.0f : 0.25f);
        const bool killed = (prog == 0 && proc.isFxKilled (p1Slot (k))) || (prog == 3 && k >= 4 && proc.isV10Killed (k - 4));
        s.getProperties().set ("arc", (juce::int64) (killed ? juce::Colour (0xff5a2a2a) : orange).getARGB());
    }
    const bool p3 = prog == 2;
    noteBtn.setEnabled (p3); pcBtn.setEnabled (p3);
    noteBtn.setToggleState (! p3 || proc.getPatternPage() == 0, juce::dontSendNotification);
    pcBtn.setToggleState (p3 && proc.getPatternPage() == 1, juce::dontSendNotification);
    noteBtn.setButtonText (p3 ? juce::String::fromUTF8 ("NOTE · 1-8") : juce::String ("NOTE"));
    pcBtn.setButtonText (p3 ? juce::String::fromUTF8 ("PROG CHNG · 9-16") : juce::String ("PROG CHNG"));
    fxInputBtn.setToggleState (proc.fxInputOn(), juce::dontSendNotification);
    fxHydraBtn.setToggleState (proc.fxHydraOn(), juce::dontSendNotification);
    setupBtn.setButtonText (proc.getLearnStep() == 9 ? "SKIP (FIN)" : proc.getLearnStep() > 0 ? "CANCEL SETUP" : "SETUP LPD8");
    setupBtn.setToggleState (proc.getLearnStep() > 0, juce::dontSendNotification);
    ccBtn.setToggleState (proc.getKillFlash() > 0.0f, juce::dontSendNotification);
    bpmSlider.setEnabled (proc.getSyncSource() == 0);
    shownProg = prog;
    // meter ballistics: instant attack, ~20 dB/s release, peak hold 1.5 s
    for (int c = 0; c < 2; ++c)
    {
        const float pk = proc.takePeak (c);
        meter[c] = std::max (pk, meter[c] * 0.86f);
        if (pk >= peakHold[c] || --holdTicks[c] <= 0) { peakHold[c] = pk; holdTicks[c] = 45; }
        if (pk > 0.89f) clipTicks = 45;   // the output limiter is working
    }
    if (clipTicks > 0) --clipTicks;
    body.repaint();
}

void PadEditor::paint (juce::Graphics& g) { g.fillAll (desk); }

void PadEditor::paintBody (juce::Graphics& g)
{
    g.fillAll (desk);
    const auto b = kBody;
    const int prog = proc.getProgram();

    // body
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (b.translated (0.0f, 5.0f), 18.0f);
    g.setGradientFill (juce::ColourGradient (bodyTop, 0.0f, b.getY(), bodyBot, 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, 18.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (b.reduced (0.5f), 18.0f, 1.0f);
    g.setColour (orange);
    g.fillRoundedRectangle (juce::Rectangle<float> (b.getX(), b.getY() + 26.0f, 3.0f, b.getHeight() - 52.0f), 1.5f);

    // logo
    {
        const auto lr = juce::Rectangle<float> (42.0f, 40.0f, 210.0f, 40.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, lr.getX(), 0.0f, juce::Colour (0xffb9c3ff), lr.getX() + 170.0f, 0.0f, false));
        g.setFont (logoFont (29.0f));
        g.drawText ("HYDRA", lr, juce::Justification::centredLeft);
        g.setColour (orange);
        g.setFont (labelFont (10.0f, false, 0.34f));
        g.drawText ("AIO TOOLBOX", juce::Rectangle<float> (43.0f, 84.0f, 210.0f, 14.0f), juce::Justification::centredLeft);
    }

    // program well
    {
        const auto pr = juce::Rectangle<float> (42.0f, 120.0f, 190.0f, 62.0f);
        g.setColour (well);
        g.fillRoundedRectangle (pr, 10.0f);
        g.setColour (wellEdge);
        g.drawRoundedRectangle (pr.reduced (0.5f), 10.0f, 1.0f);
        g.setColour (orange);
        g.setFont (labelFont (9.5f, true, 0.16f));
        g.drawText ("PROGRAM " + juce::String (prog + 1), pr.withHeight (30.0f).translated (0.0f, 4.0f), juce::Justification::centred);
        g.setColour (print);
        g.setFont (labelFont (13.5f, true, 0.04f));
        g.drawText (programName (prog), pr.withTrimmedTop (28.0f).withTrimmedBottom (6.0f), juce::Justification::centred);
    }

    // pad print
    for (int i = 0; i < 8; ++i)
    {
        const auto pbn = padBounds (i).toFloat().translated (3.0f, -17.0f).withHeight (13.0f);
        g.setColour (printFaint);
        g.setFont (labelFont (9.0f, true, 0.14f));
        g.drawText (prog == 2 ? juce::String ("STEP") : "PAD " + juce::String (i + 1), pbn, juce::Justification::centredLeft);
    }

    // knob print: id, function (changes with the program), value
    for (int k = 0; k < 8; ++k)
    {
        const auto kb = knobBounds (k).toFloat();
        const auto label = juce::String (knobLabel (prog, k));
        g.setColour (printFaint);
        g.setFont (labelFont (9.0f, true, 0.12f));
        g.drawText ("K" + juce::String (k + 1), juce::Rectangle<float> (kb.getCentreX() - 40.0f, kb.getY() - 38.0f, 80.0f, 12.0f), juce::Justification::centred);
        if (label.isEmpty()) continue;
        const bool killed = (prog == 0 && proc.isFxKilled (p1Slot (k))) || (prog == 3 && k >= 4 && proc.isV10Killed (k - 4));
        g.setColour (killed ? red : orange);
        g.setFont (labelFont (9.5f, true, 0.1f));
        g.drawFittedText (label, juce::Rectangle<int> ((int) kb.getCentreX() - 44, (int) kb.getY() - 23, 88, 13), juce::Justification::centred, 1, 0.7f);
        g.setColour (killed ? red.withAlpha (0.8f) : printDim);
        g.setFont (monoFont (10.0f));
        g.drawText (killed ? juce::String ("KILLED") : proc.getKnobText (prog, k), juce::Rectangle<float> (kb.getCentreX() - 44.0f, kb.getBottom() + 3.0f, 88.0f, 13.0f), juce::Justification::centred);
    }

    // setup assistant
    if (const int step = proc.getLearnStep(); step > 0)
    {
        const auto banner = juce::Rectangle<float> (250.0f, 20.0f, 790.0f, 30.0f);
        g.setColour (orange);
        g.fillRoundedRectangle (banner, 8.0f);
        g.setColour (juce::Colour (0xff16110c));
        g.setFont (labelFont (12.0f, true, 0.02f));
        static const char* msgs[] = { "",
            "SETUP 1/9 : sur le LPD8, PROGRAM + PAD 1, puis appuie sur PAD 1",
            "SETUP 2/9 : programme 1, tourne K1",
            "SETUP 3/9 : PROGRAM + PAD 2, puis appuie sur PAD 1",
            "SETUP 4/9 : programme 2, tourne K1",
            "SETUP 5/9 : PROGRAM + PAD 3, puis appuie sur PAD 1 (mode NOTE)",
            "SETUP 6/9 : programme 3, tourne K1",
            "SETUP 7/9 : PROGRAM + PAD 4, puis appuie sur PAD 1",
            "SETUP 8/9 : programme 4, tourne K1",
            "SETUP 9/9 : PROGRAM + PAD 3, PROG CHNG puis PAD 1 (ou SKIP)" };
        g.drawText (juce::String::fromUTF8 (msgs[juce::jlimit (0, 9, step)]), banner, juce::Justification::centred);
    }

    // KILL flash
    if (const float kf = proc.getKillFlash(); kf > 0.0f)
    {
        g.setColour (red.withAlpha (0.5f * kf));
        g.drawRoundedRectangle (b.reduced (1.5f), 18.0f, 3.0f);
    }

    // ---- side panel ----
    const float px = kPanelX;
    {
        const auto pnl = juce::Rectangle<float> (px, 14.0f, 300.0f, 330.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (pnl.translated (0.0f, 5.0f), 18.0f);
        g.setColour (panel);
        g.fillRoundedRectangle (pnl, 18.0f);
        g.setColour (panelEdge);
        g.drawRoundedRectangle (pnl.reduced (0.5f), 18.0f, 1.0f);
    }
    // status screen
    {
        const auto sc = juce::Rectangle<float> (px + 14.0f, 26.0f, 272.0f, 72.0f);
        g.setColour (well);
        g.fillRoundedRectangle (sc, 10.0f);
        g.setColour (wellEdge);
        g.drawRoundedRectangle (sc.reduced (0.5f), 10.0f, 1.0f);
        const int src = proc.getSyncSource();
        const juce::String srcName = src == 1 ? "MIDI CLOCK" : src == 3 ? "LINK" : src == 2 ? "HOST" : "INTERNAL";
        g.setColour (src == 1 || src == 3 ? green : orange);
        g.setFont (labelFont (9.5f, true, 0.16f));
        g.drawText (srcName, juce::Rectangle<float> (sc.getX() + 12.0f, sc.getY() + 8.0f, 120.0f, 12.0f), juce::Justification::centredLeft);
        g.setColour (print);
        g.setFont (labelFont (21.0f, true, 0.0f));
        const juce::String bpmTxt = juce::String (proc.getBpm(), 1);
        g.drawText (bpmTxt, juce::Rectangle<float> (sc.getX() + 12.0f, sc.getY() + 20.0f, 90.0f, 26.0f), juce::Justification::centredLeft);
        g.setColour (printFaint);
        g.setFont (labelFont (9.5f, true, 0.12f));
        g.drawText ("BPM", juce::Rectangle<float> (sc.getX() + 12.0f + labelFont (21.0f, true, 0.0f).getStringWidthFloat (bpmTxt) + 6.0f, sc.getY() + 28.0f, 40.0f, 14.0f), juce::Justification::centredLeft);
        const int beatNow = (int) proc.getBarBeat();
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (i == beatNow ? (i == 0 ? red : orange) : track);
            g.fillEllipse (sc.getRight() - 72.0f + 16.0f * (float) i, sc.getY() + 12.0f, 9.0f, 9.0f);
        }
        const int peers = proc.getLinkPeers();
        g.setColour (printDim);
        g.setFont (monoFont (9.5f));
        g.drawFittedText (proc.getMidiStatus() + juce::String::fromUTF8 (" · LINK ") + (peers > 0 ? juce::String (peers) + " PEER" + (peers > 1 ? "S" : "") : juce::String ("WAITING")),
                          juce::Rectangle<int> ((int) sc.getX() + 12, (int) sc.getY() + 46, 250, 12), juce::Justification::centredLeft, 1, 0.7f);
        const auto ev = proc.getLastEvent();
        g.setColour (printFaint);
        g.drawFittedText ((ev.isNotEmpty() ? ev + juce::String::fromUTF8 (" · ") : juce::String()) + "V" + Updater::currentVersion(),
                          juce::Rectangle<int> ((int) sc.getX() + 12, (int) sc.getY() + 58, 250, 11), juce::Justification::centredLeft, 1, 0.7f);
    }
    g.setFont (labelFont (9.0f, true, 0.1f));
    g.setColour (printFaint);
    g.drawText ("SYNC OFFSET", juce::Rectangle<float> (px + 16.0f, 134.0f, 70.0f, 12.0f), juce::Justification::centredLeft);
    g.drawText ("TEMPO", juce::Rectangle<float> (px + 158.0f, 134.0f, 40.0f, 12.0f), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffc9ccd2));
    g.setFont (monoFont (9.5f));
    g.drawText (proc.apvts.getParameter (id::syncOffset)->getCurrentValueAsText(), juce::Rectangle<float> (px + 86.0f, 134.0f, 56.0f, 12.0f), juce::Justification::centredRight);
    g.drawText (proc.getSyncSource() == 0 ? juce::String (proc.apvts.getRawParameterValue (id::bpm)->load(), 1) : juce::String ("SYNC"),
                juce::Rectangle<float> (px + 198.0f, 134.0f, 86.0f, 12.0f), juce::Justification::centredRight);
    // master
    {
        const float mdb = PadProcessor::masterDb (proc.apvts.getRawParameterValue (id::master)->load());
        g.setColour (printFaint);
        g.setFont (labelFont (9.0f, true, 0.14f));
        g.drawText ("MASTER", juce::Rectangle<float> (px + 9.0f, 226.0f, 80.0f, 12.0f), juce::Justification::centred);
        g.setColour (print);
        g.setFont (labelFont (13.0f, true, 0.0f));
        g.drawText ((mdb <= -99.0f ? juce::String ("-INF") : (mdb > 0.05f ? "+" : "") + juce::String (mdb, 1)) + " dB",
                    juce::Rectangle<float> (px + 9.0f, 240.0f, 80.0f, 16.0f), juce::Justification::centred);
    }
    // segmented stereo meter, -40 .. +3 dB
    {
        constexpr int nSeg = 20;
        const float top = 158.0f, segH = 3.6f, step = 5.0f, lo = -40.0f, hi = 3.0f;
        auto toDb = [] (float a) { return a > 1.0e-5f ? 20.0f * std::log10 (a) : -100.0f; };
        for (int c = 0; c < 2; ++c)
        {
            const float x = px + 98.0f + 16.0f * (float) c;
            const float lvl = toDb (meter[c]), hold = toDb (peakHold[c]);
            const int lit = (int) std::floor ((juce::jlimit (lo, hi, lvl) - lo) / (hi - lo) * (float) nSeg + 0.5f);
            const int holdSeg = (int) std::floor ((juce::jlimit (lo, hi, hold) - lo) / (hi - lo) * (float) nSeg - 0.5f);
            for (int i = 0; i < nSeg; ++i)
            {
                const auto col = i >= 18 ? red : i >= 14 ? juce::Colour (0xffffb020) : green;
                const bool on = (i < lit && lvl > lo) || (i == holdSeg && hold > lo);
                g.setColour (on ? col : col.withAlpha (0.12f));
                g.fillRoundedRectangle (x, top + (float) (nSeg - 1 - i) * step, 12.0f, segH, 1.0f);
            }
        }
        g.setColour (printFaint);
        g.setFont (labelFont (8.0f, true, 0.1f));
        g.drawText ("L", juce::Rectangle<float> (px + 98.0f, 260.0f, 12.0f, 10.0f), juce::Justification::centred);
        g.drawText ("R", juce::Rectangle<float> (px + 114.0f, 260.0f, 12.0f, 10.0f), juce::Justification::centred);
    }
    g.setColour (printFaint);
    g.setFont (labelFont (9.0f, true, 0.14f));
    g.drawText ("FX SOURCE", juce::Rectangle<float> (px + 147.0f, 158.0f, 120.0f, 12.0f), juce::Justification::centredLeft);
    {
        auto toDb = [] (float a) { return a > 1.0e-5f ? 20.0f * std::log10 (a) : -100.0f; };
        const float pk = toDb (std::max (peakHold[0], peakHold[1]));
        g.setColour (clipTicks > 0 ? red : printDim);
        g.setFont (monoFont (9.5f));
        g.drawText (clipTicks > 0 ? juce::String ("LIMIT") : "PEAK " + (pk <= -99.0f ? juce::String ("-INF") : juce::String (pk, 1)),
                    juce::Rectangle<float> (px + 147.0f, 252.0f, 137.0f, 12.0f), juce::Justification::centredLeft);
    }
}
