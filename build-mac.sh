#!/bin/bash
# Compile Brume + Hydra on this Mac and install them (no GitHub Actions needed).
# Usage, in Terminal:  bash ~/Downloads/brume-src/build-mac.sh
set -e
cd "$(dirname "$0")"
VERSION="${BRUME_VERSION:-1.0.31}"

echo "== 1/4 Outils"
if ! xcode-select -p >/dev/null 2>&1; then
    echo "Les outils de développement Apple ne sont pas installés : une fenêtre va s'ouvrir, clique sur Installer,"
    echo "puis relance ce script une fois l'installation terminée (environ 10 minutes)."
    xcode-select --install || true
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    for d in "$HOME"/Library/Python/*/bin /opt/homebrew/bin /usr/local/bin; do [ -x "$d/cmake" ] && export PATH="$d:$PATH"; done
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo "Installation de CMake (une seule fois)..."
    python3 -m pip install --user --quiet cmake
    for d in "$HOME"/Library/Python/*/bin; do export PATH="$d:$PATH"; done
fi
cmake --version | head -1

echo "== 2/4 Configuration (la 1re fois, téléchargement de JUCE et Ableton Link : quelques minutes)"
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release -DBRUME_VERSION="$VERSION" >/dev/null

echo "== 3/4 Compilation (10 à 20 min la 1re fois, ensuite 1 à 3 min)"
cmake --build build-mac --config Release --parallel "$(sysctl -n hw.ncpu)" \
    --target Brume_VST3 Brume_AU Brume_Standalone BrumePad_VST3 BrumePad_AU BrumePad_Standalone 2>&1 \
    | grep -E "error|warning: unused|Linking|Built target" || true
[ -d "build-mac/BrumePad_artefacts/Release/Standalone/Hydra.app" ] || { echo "ÉCHEC de la compilation : envoie-moi la sortie ci-dessus."; exit 1; }

echo "== 4/4 Installation"
osascript -e 'quit app "Hydra"' -e 'quit app "Brume"' 2>/dev/null || true
sleep 1
P="$HOME/Library/Audio/Plug-Ins"
mkdir -p "$P/VST3" "$P/Components"
for name in "Brume" "Hydra"; do
    t="$name"; [ "$name" = "Hydra" ] && t="BrumePad"
    A="build-mac/${t}_artefacts/Release"
    rm -rf "$P/VST3/Brume Pad.vst3" "$P/Components/Brume Pad.component" "/Applications/Brume Pad.app"
    rm -rf "$P/VST3/$name.vst3" "$P/Components/$name.component" "/Applications/$name.app"
    ditto "$A/VST3/$name.vst3" "$P/VST3/$name.vst3"
    ditto "$A/AU/$name.component" "$P/Components/$name.component"
    ditto "$A/Standalone/$name.app" "/Applications/$name.app"
    codesign --force --deep -s - "$P/VST3/$name.vst3" "$P/Components/$name.component" "/Applications/$name.app" 2>/dev/null
    xattr -cr "$P/VST3/$name.vst3" "$P/Components/$name.component" "/Applications/$name.app"
    echo "   $name installé (app + VST3 + AU)"
done
killall -9 AudioComponentRegistrar 2>/dev/null || true   # Traktor / Ableton revoient les AU tout de suite
echo "Terminé. Lance Hydra depuis Applications."
open -a "Hydra"
