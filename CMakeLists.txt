cmake_minimum_required(VERSION 3.22)
project(Huldra VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()

# Plugin formats per platform: VST3 + Standalone everywhere, AU on macOS, LV2 on Linux.
set(HULDRA_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND HULDRA_FORMATS AU)
elseif(UNIX)
    list(APPEND HULDRA_FORMATS LV2)
endif()

# JUCE 8 is not part of this repository. A local checkout in external/JUCE (or -DHULDRA_JUCE_DIR=<path>) is used
# if present; otherwise the pinned release is downloaded when CMake configures.
set(HULDRA_JUCE_TAG "9.0.4")
set(HULDRA_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" CACHE PATH "Local JUCE checkout (optional)")
if(EXISTS "${HULDRA_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory(${HULDRA_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                              GIT_TAG ${HULDRA_JUCE_TAG} GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Git commit shown in the About panel (empty outside a git checkout)
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE HULDRA_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

# Copy the built plugins to the user's plugin folders (turn off in CI: -DHULDRA_COPY_PLUGIN=OFF)
option(HULDRA_COPY_PLUGIN "Install the plugins to the user plugin folders after building" ON)

juce_add_plugin(Huldra
    COMPANY_NAME "Fjord"
    BUNDLE_ID "com.fjord.huldra"
    PLUGIN_MANUFACTURER_CODE Fjrd
    PLUGIN_CODE Hldr
    FORMATS ${HULDRA_FORMATS}
    LV2URI "https://github.com/fjord-sounds/huldra"
    PRODUCT_NAME "Huldra"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"
    VERSION ${PROJECT_VERSION}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Huldra records your voice or instrument to turn it into a playable sound."
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD ${HULDRA_COPY_PLUGIN})

file(GLOB_RECURSE HULDRA_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)
target_sources(Huldra PRIVATE ${HULDRA_SOURCES})
set(HULDRA_INCLUDES Source external/signalsmith-stretch external/signalsmith-linear)
target_include_directories(Huldra PRIVATE ${HULDRA_INCLUDES})

# Factory presets are embedded in the plugin.
file(GLOB_RECURSE HULDRA_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/Factory/*.huldra)
# Font: Manrope (SIL Open Font License, see resources/fonts/OFL.txt)
file(GLOB HULDRA_FONTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/*.ttf)
juce_add_binary_data(HuldraBinaryData SOURCES ${HULDRA_PRESETS} ${HULDRA_FONTS})
set_target_properties(HuldraBinaryData PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

set(HULDRA_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_MP3AUDIOFORMAT=1)
if(APPLE)
    list(APPEND HULDRA_DEFINITIONS SIGNALSMITH_USE_ACCELERATE=1)
endif()
target_compile_definitions(Huldra PUBLIC ${HULDRA_DEFINITIONS})
if(HULDRA_GIT_HASH)
    target_compile_definitions(Huldra PUBLIC HULDRA_GIT_HASH="${HULDRA_GIT_HASH}")
endif()

target_link_libraries(Huldra
    PRIVATE HuldraBinaryData juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Offline render test (no DAW): cmake --build build --target HuldraRenderTest
juce_add_console_app(HuldraRenderTest PRODUCT_NAME "HuldraRenderTest")
target_sources(HuldraRenderTest PRIVATE tests/RenderTest.cpp ${HULDRA_SOURCES})
target_include_directories(HuldraRenderTest PRIVATE ${HULDRA_INCLUDES})
target_compile_definitions(HuldraRenderTest PRIVATE ${HULDRA_DEFINITIONS}
    JucePlugin_Name="Huldra" JucePlugin_VersionString="${PROJECT_VERSION}" JucePlugin_Build_Standalone=0)
if(HULDRA_GIT_HASH)
    target_compile_definitions(HuldraRenderTest PRIVATE HULDRA_GIT_HASH="${HULDRA_GIT_HASH}")
endif()
target_link_libraries(HuldraRenderTest PRIVATE
    HuldraBinaryData juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats juce::juce_recommended_config_flags)
if(APPLE)
    set_target_properties(HuldraRenderTest PROPERTIES OSX_ARCHITECTURES arm64)
endif()
