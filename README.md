# Brume

Pédale réverbe stéréo + bruit, en plugin VST3 / AU (JUCE 8).

## Compiler

**Automatique (recommandé)** : pousser ce dossier sur un dépôt GitHub. Le workflow
`.github/workflows/build.yml` compile Windows et macOS et publie deux archives
(onglet *Actions* > dernier build > *Artifacts*).

**Windows, à la main** : installer Visual Studio 2022 (charge de travail « Développement Desktop en C++ ») et CMake, puis :

```
cmake -S . -B build
cmake --build build --config Release --target Brume_VST3
```

Le plugin est dans `build/Brume_artefacts/Release/VST3/Brume.vst3`.

**macOS, à la main** : installer Xcode et CMake, puis les mêmes commandes (ajouter `--target Brume_AU` pour l'Audio Unit).

Installation dans Ableton : voir `INSTALL.txt`.

## Structure

- `Source/PluginProcessor.*` : moteur audio (réverbe à convolution, bruit, trémolo, filtre, on/off avec queue)
- `Source/PluginEditor.*` : interface pédale
- `Tests/LevelTest.cpp` : test hors-ligne des niveaux (`-DBRUME_BUILD_TESTS=ON`)
