# ---------------------------------------------------------------------------
#  SpectroCheck - espectrograma para ver hasta dónde llega un audio y si
#  tiene el corte típico de MP3/AAC.
#  Este archivo le dice a CMake cómo armar el plugin. Descarga JUCE solo,
#  así que no hace falta instalar JUCE a mano.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.22)

# En Mac: compilar para Apple Silicon e Intel, compatible desde macOS 11.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Versión mínima de macOS")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Arquitecturas")

project(SpectroCheck VERSION 1.0.1 LANGUAGES C CXX)   # subir la versión hace que Logic vuelva a leer el nombre

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Descarga JUCE la primera vez que configurás el proyecto.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

option(SPECTROCHECK_PLUGIN "Compilar el plugin" ON)
option(SPECTROCHECK_TESTS  "Compilar los tests" OFF)

if(SPECTROCHECK_PLUGIN)
    juce_add_plugin(SpectroCheck
        COMPANY_NAME             "0600musica"
        BUNDLE_ID                "com.0600musica.spectrocheck"
        PLUGIN_MANUFACTURER_CODE Ms06        # 4 letras, al menos una mayúscula
        PLUGIN_CODE              Spck        # 4 letras, única para este plugin
        PRODUCT_NAME             "SpectroCheck"
        PLUGIN_NAME              "SpectroCheck"
        DESCRIPTION              "Espectrograma para detectar cortes de MP3/AAC"
        FORMATS                  AU VST3
        IS_SYNTH                 FALSE
        NEEDS_MIDI_INPUT         FALSE
        NEEDS_MIDI_OUTPUT        FALSE
        VST3_CATEGORIES          Analyzer
        AU_MAIN_TYPE             kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD  TRUE)

    # Letra Inter (licencia OFL, ver Resources/Fonts/OFL.txt), incluida en el plugin.
    juce_add_binary_data(SpectroCheckFonts SOURCES
        Resources/Fonts/Inter-Regular.otf
        Resources/Fonts/Inter-Medium.otf
        Resources/Fonts/Inter-SemiBold.otf)
    set_target_properties(SpectroCheckFonts PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

    target_sources(SpectroCheck PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

    target_include_directories(SpectroCheck PRIVATE Source)

    target_compile_definitions(SpectroCheck PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(SpectroCheck
        PRIVATE
            SpectroCheckFonts
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
#  Tests automáticos (opcionales): cmake -B build -DSPECTROCHECK_TESTS=ON
# ---------------------------------------------------------------------------
if(SPECTROCHECK_TESTS)
    juce_add_console_app(SpectroCheckTests PRODUCT_NAME "SpectroCheckTests")
    target_sources(SpectroCheckTests PRIVATE Tests/SpectroCheckTest.cpp)
    target_include_directories(SpectroCheckTests PRIVATE Source)
    target_compile_definitions(SpectroCheckTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(SpectroCheckTests PRIVATE
        juce::juce_dsp juce::juce_recommended_config_flags)
endif()
