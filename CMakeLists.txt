cmake_minimum_required(VERSION 3.22)
project(SONARA VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.12)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SONARA
    COMPANY_NAME "SONARA"
    BUNDLE_ID "com.sonara.instrument"
    PLUGIN_MANUFACTURER_CODE Sona
    PLUGIN_CODE Snra
    FORMATS VST3 Standalone
    PRODUCT_NAME "SONARA"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(SONARA)

target_sources(SONARA PRIVATE
    Source/PluginProcessor.cpp Source/PluginProcessor.h
    Source/PluginEditor.cpp Source/PluginEditor.h
    Source/Engine/SoundDNA.cpp Source/Engine/SoundDNA.h
    Source/Engine/SonaraEngine.cpp Source/Engine/SonaraEngine.h
    Source/Generation/PromptGenerator.cpp Source/Generation/PromptGenerator.h
    Source/Generation/SongArrangement.cpp Source/Generation/SongArrangement.h
    Source/Integration/CyanoryxBridge.h
    Source/Reference/ReferenceAnalyzer.cpp Source/Reference/ReferenceAnalyzer.h
    Source/Export/AudioExporter.cpp Source/Export/AudioExporter.h
    Source/Song/DrumSynth.cpp Source/Song/DrumSynth.h)

target_compile_definitions(SONARA PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SONARA PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

include(CTest)
if(BUILD_TESTING)
    juce_add_console_app(SONARA_Tests PRODUCT_NAME "SONARA Tests")
    juce_generate_juce_header(SONARA_Tests)
    target_sources(SONARA_Tests PRIVATE
        Tests/MutationLocksTests.cpp
        Source/Engine/SoundDNA.cpp Source/Engine/SoundDNA.h
        Source/Generation/PromptGenerator.cpp Source/Generation/PromptGenerator.h)
    target_compile_definitions(SONARA_Tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(SONARA_Tests PRIVATE
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME sonara_mutation_locks COMMAND SONARA_Tests)

    juce_add_console_app(SONARA_BridgeTests PRODUCT_NAME "SONARA Bridge Tests")
    juce_generate_juce_header(SONARA_BridgeTests)
    target_sources(SONARA_BridgeTests PRIVATE
        Tests/CyanoryxBridgeTests.cpp
        Source/Engine/SoundDNA.cpp Source/Engine/SoundDNA.h
        Source/Integration/CyanoryxBridge.h)
    target_compile_definitions(SONARA_BridgeTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(SONARA_BridgeTests PRIVATE
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME sonara_cyanoryx_bridge COMMAND SONARA_BridgeTests)

    juce_add_console_app(SONARA_SeedTests PRODUCT_NAME "SONARA Seed Tests")
    juce_generate_juce_header(SONARA_SeedTests)
    target_sources(SONARA_SeedTests PRIVATE
        Tests/SoundDNASeedTests.cpp
        Source/Engine/SoundDNA.cpp Source/Engine/SoundDNA.h)
    target_compile_definitions(SONARA_SeedTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(SONARA_SeedTests PRIVATE
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME sonara_sounddna_seed_state COMMAND SONARA_SeedTests)

    juce_add_console_app(SONARA_EngineTests PRODUCT_NAME "SONARA Engine Tests")
    juce_generate_juce_header(SONARA_EngineTests)
    target_sources(SONARA_EngineTests PRIVATE
        Tests/EngineSmokeTests.cpp
        Source/Engine/SonaraEngine.cpp Source/Engine/SonaraEngine.h
        Source/Engine/SoundDNA.cpp Source/Engine/SoundDNA.h)
    target_compile_definitions(SONARA_EngineTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(SONARA_EngineTests PRIVATE
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME sonara_engine_dsp_smoke COMMAND SONARA_EngineTests)

    juce_add_console_app(SONARA_ArrangementTests PRODUCT_NAME "SONARA Arrangement Tests")
    juce_generate_juce_header(SONARA_ArrangementTests)
    target_sources(SONARA_ArrangementTests PRIVATE
        Tests/SongArrangementTests.cpp
        Source/Generation/SongArrangement.cpp Source/Generation/SongArrangement.h
        Source/Generation/PromptGenerator.cpp Source/Generation/PromptGenerator.h
        Source/Engine/SoundDNA.cpp Source/Engine/SoundDNA.h)
    target_compile_definitions(SONARA_ArrangementTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(SONARA_ArrangementTests PRIVATE
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME sonara_arrangement_generation COMMAND SONARA_ArrangementTests)

    juce_add_console_app(SONARA_PlaybackTests PRODUCT_NAME "SONARA Playback Acceptance Tests")
    juce_generate_juce_header(SONARA_PlaybackTests)
    target_sources(SONARA_PlaybackTests PRIVATE
        Tests/PlaybackIntegrationTests.cpp
        Source/PluginProcessor.cpp Source/PluginProcessor.h
        Source/Engine/SonaraEngine.cpp Source/Engine/SonaraEngine.h
        Source/Engine/SoundDNA.cpp Source/Engine/SoundDNA.h
        Source/Generation/PromptGenerator.cpp Source/Generation/PromptGenerator.h
        Source/Generation/SongArrangement.cpp Source/Generation/SongArrangement.h
        Source/Integration/CyanoryxBridge.h
        Source/Song/DrumSynth.cpp Source/Song/DrumSynth.h
        Source/Reference/ReferenceAnalyzer.cpp Source/Reference/ReferenceAnalyzer.h
        Source/Export/AudioExporter.cpp Source/Export/AudioExporter.h)
    target_compile_definitions(SONARA_PlaybackTests PRIVATE
        SONARA_HEADLESS_TEST=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(SONARA_PlaybackTests PRIVATE
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME sonara_full_playback_acceptance COMMAND SONARA_PlaybackTests)
endif()