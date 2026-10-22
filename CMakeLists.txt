cmake_minimum_required(VERSION 3.22)
set(BRUME_VERSION "1.0.0" CACHE STRING "Version (set by CI from the build number)")
project(Brume VERSION ${BRUME_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE: use a local checkout with -DJUCE_DIR=/path/to/JUCE, otherwise download it.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(BRUME_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND BRUME_FORMATS AU)
endif()

juce_add_plugin(Brume
    COMPANY_NAME "Brume Audio"
    BUNDLE_ID "audio.brume.pedal"
    PLUGIN_MANUFACTURER_CODE Brum
    PLUGIN_CODE Brm1
    FORMATS ${BRUME_FORMATS}
    PRODUCT_NAME "Brume"
    ICON_BIG Resources/icon.png
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect
    # macOS only lets an app read an audio input (BlackHole, a sound card) once it has this permission
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Brume a besoin de l'entree audio pour traiter le son de Traktor (BlackHole).")

# Michroma font for the logo plate (SIL Open Font License, Resources/OFL-Michroma.txt)
juce_add_binary_data(BrumeAssets SOURCES Resources/Michroma-Regular.ttf
    Resources/rmx_kick.wav Resources/rmx_snare.wav Resources/rmx_clap.wav Resources/rmx_hat.wav
    Resources/revolo_noise.wav
    Resources/Syncopate-Bold.ttf Resources/Inter-SemiBold.ttf Resources/Inter-Bold.ttf Resources/JetBrainsMono-Medium.ttf)
set_target_properties(BrumeAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(Brume PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Updater.cpp
    Source/F1Reader.cpp)

# Direct USB (HID) reading of the Traktor Kontrol F1, via hidapi (BSD licence, ThirdParty/hidapi)
option(BRUME_LINK_LINUX "Build Ableton Link on Linux too (tests)" OFF)
option(BRUME_F1_LINUX "Build the F1 HID reader on Linux too (needs libudev, for testing)" OFF)
function(brume_add_hidapi target)
    target_include_directories(${target} PRIVATE ThirdParty/hidapi/hidapi ThirdParty/hidapi/mac)
    if(APPLE)
        target_sources(${target} PRIVATE ThirdParty/hidapi/mac/hid.c)
        target_link_libraries(${target} PRIVATE "-framework IOKit" "-framework CoreFoundation")
        target_compile_definitions(${target} PRIVATE BRUME_HAS_HIDAPI=1)
    elseif(BRUME_F1_LINUX)
        target_sources(${target} PRIVATE ThirdParty/hidapi/linux/hid.c)
        target_link_libraries(${target} PRIVATE udev)
        target_compile_definitions(${target} PRIVATE BRUME_HAS_HIDAPI=1)
    endif()
endfunction()
brume_add_hidapi(Brume)

# In-app updates (macOS standalone): read-only GitHub token passed by CI from the repo secret, never committed.
if(APPLE AND DEFINED ENV{BRUME_UPDATE_TOKEN} AND NOT "$ENV{BRUME_UPDATE_TOKEN}" STREQUAL "")
    target_compile_definitions(Brume PRIVATE BRUME_UPDATE_TOKEN="$ENV{BRUME_UPDATE_TOKEN}")
endif()

target_compile_definitions(Brume PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Brume
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
        BrumeAssets)

# ---------------------------------------------------------------------------
# Hydra (target BrumePad, ex "Brume Pad"): an Akai LPD8 MK2 as an all-in-one box (X-PAD, send FX, white noise, step sequencer), same release archive
juce_add_plugin(BrumePad
    COMPANY_NAME "Brume Audio"
    BUNDLE_ID "audio.brume.pad"
    PLUGIN_MANUFACTURER_CODE Brum
    PLUGIN_CODE Brm3
    FORMATS ${BRUME_FORMATS}
    PRODUCT_NAME "Hydra"
    PLUGIN_NAME "Hydra"
    ICON_BIG Resources/icon-hydra.png
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Hydra a besoin de l'entree audio pour traiter le son de Traktor (BlackHole)."
    PLIST_TO_MERGE "<plist><dict><key>NSLocalNetworkUsageDescription</key><string>Hydra utilise Ableton Link pour suivre le tempo de Traktor.</string></dict></plist>")

target_sources(BrumePad PRIVATE
    Pad/PadProcessor.cpp
    Pad/PadEditor.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Updater.cpp
    Source/F1Reader.cpp)
brume_add_hidapi(BrumePad)

# Ableton Link (Traktor's LINK button): tempo and beat over the network, Mac only (GPLv2, personal use)
if(APPLE OR BRUME_LINK_LINUX)
    include(FetchContent)
    FetchContent_Declare(abletonlink
        GIT_REPOSITORY https://github.com/Ableton/link.git
        GIT_TAG Link-3.1.5
        GIT_SHALLOW ON
        GIT_SUBMODULES modules/asio-standalone)
    FetchContent_GetProperties(abletonlink)
    if(NOT abletonlink_POPULATED)
        FetchContent_Populate(abletonlink)
    endif()
    include(${abletonlink_SOURCE_DIR}/AbletonLinkConfig.cmake)
    set(BRUME_HAS_LINK ON)
endif()
function(brume_add_link target)
    if(BRUME_HAS_LINK)
        target_link_libraries(${target} PRIVATE Ableton::Link)
        target_compile_definitions(${target} PRIVATE BRUME_HAS_LINK=1)
    endif()
endfunction()
brume_add_link(BrumePad)
if(APPLE AND DEFINED ENV{BRUME_UPDATE_TOKEN} AND NOT "$ENV{BRUME_UPDATE_TOKEN}" STREQUAL "")
    target_compile_definitions(BrumePad PRIVATE BRUME_UPDATE_TOKEN="$ENV{BRUME_UPDATE_TOKEN}")
endif()
target_compile_definitions(BrumePad PUBLIC
    BRUME_EMBEDDED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(BrumePad
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
        BrumeAssets)

# Offline level test (not shipped): renders noise through the processor and prints levels.
option(BRUME_BUILD_TESTS "Build the offline level test" OFF)
if(BRUME_BUILD_TESTS)
    juce_add_console_app(BrumeTest PRODUCT_NAME "BrumeTest")
    target_sources(BrumeTest PRIVATE Tests/LevelTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Updater.cpp Source/F1Reader.cpp)
    brume_add_hidapi(BrumeTest)
    target_compile_definitions(BrumeTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 BRUME_TEST=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(BrumeTest PRIVATE BrumeAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

    juce_add_console_app(PadTest PRODUCT_NAME "PadTest")
    target_sources(PadTest PRIVATE Tests/PadTest.cpp Pad/PadProcessor.cpp Pad/PadEditor.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Updater.cpp Source/F1Reader.cpp)
    brume_add_hidapi(PadTest)
    brume_add_link(PadTest)
    target_compile_definitions(PadTest PRIVATE BRUME_EMBEDDED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 BRUME_TEST=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(PadTest PRIVATE BrumeAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
