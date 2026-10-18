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
juce_add_binary_data(BrumeAssets SOURCES Resources/Michroma-Regular.ttf)
set_target_properties(BrumeAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(Brume PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Updater.cpp
    Source/F1Reader.cpp)

# Direct USB (HID) reading of the Traktor Kontrol F1, via hidapi (BSD licence, ThirdParty/hidapi)
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

# Offline level test (not shipped): renders noise through the processor and prints levels.
option(BRUME_BUILD_TESTS "Build the offline level test" OFF)
if(BRUME_BUILD_TESTS)
    juce_add_console_app(BrumeTest PRODUCT_NAME "BrumeTest")
    target_sources(BrumeTest PRIVATE Tests/LevelTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Updater.cpp Source/F1Reader.cpp)
    brume_add_hidapi(BrumeTest)
    target_compile_definitions(BrumeTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 BRUME_TEST=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(BrumeTest PRIVATE BrumeAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
