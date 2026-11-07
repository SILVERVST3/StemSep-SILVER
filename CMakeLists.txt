cmake_minimum_required(VERSION 3.22)

project(StemSepSILVER VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# Dependencias
#   JUCE_DIR      : pasta do JUCE (se vazio, baixa o JUCE 8.0.9 do GitHub)
#   ARA_SDK_PATH  : pasta do ARA SDK 2.2.0 (obrigatoria para o suporte ARA)
#   STEMSEP_RESOURCES_DIR : pasta com StemSep_SCNet_XL.onnx, onnxruntime.dll e DirectML.dll
#                           (copiados para dentro do bundle .vst3/Contents/Resources)
# ------------------------------------------------------------------------------
set(JUCE_DIR "" CACHE PATH "Pasta do JUCE")
set(ARA_SDK_PATH "" CACHE PATH "Pasta do ARA SDK")
set(STEMSEP_RESOURCES_DIR "" CACHE PATH "Recursos (modelo ONNX + ONNX Runtime) para copiar no bundle")
option(STEMSEP_BUILD_CLI "Compila tambem a ferramenta de linha de comando de teste" ON)

if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(NOT ARA_SDK_PATH)
    message(FATAL_ERROR "Defina ARA_SDK_PATH (ARA SDK 2.2.0: git clone --branch releases/2.2.0 --recurse-submodules https://github.com/Celemony/ARA_SDK.git)")
endif()
juce_set_ara_sdk_path("${ARA_SDK_PATH}")

juce_add_plugin(StemSepSILVER
    PRODUCT_NAME "StemSep SILVER"
    COMPANY_NAME "SILVER"
    COMPANY_WEBSITE "https://github.com"
    BUNDLE_ID "com.silver.stemsep"
    PLUGIN_MANUFACTURER_CODE Slvr
    PLUGIN_CODE StSp
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    IS_ARA_EFFECT TRUE
    VST3_CATEGORIES Fx Tools
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(ENGINE_SOURCES
    source/engine/ModelStft.cpp
    source/engine/OrtModel.cpp
    source/engine/Resample.cpp
    source/engine/MixConsistency.cpp
    source/engine/Separator.cpp)

target_sources(StemSepSILVER PRIVATE
    ${ENGINE_SOURCES}
    source/plugin/SeparationService.cpp
    source/plugin/StemARA.cpp
    source/plugin/PluginProcessor.cpp
    source/plugin/PluginEditor.cpp
    source/plugin/SilverLookAndFeel.cpp)

target_include_directories(StemSepSILVER PRIVATE
    source
    third_party/pocketfft
    third_party/r8brain
    third_party/onnxruntime/include)

target_compile_definitions(StemSepSILVER PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1)

juce_generate_juce_header(StemSepSILVER)

if(MSVC)
    target_compile_options(StemSepSILVER PRIVATE /utf-8 /bigobj /W3)
endif()

target_link_libraries(StemSepSILVER
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(NOT WIN32)
    target_link_libraries(StemSepSILVER PRIVATE ${CMAKE_DL_LIBS})
endif()

# Copia modelo + ONNX Runtime para dentro do bundle VST3
if(STEMSEP_RESOURCES_DIR)
    add_custom_command(TARGET StemSepSILVER_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_PROPERTY:StemSepSILVER_VST3,JUCE_PLUGIN_ARTEFACT_FILE>/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${STEMSEP_RESOURCES_DIR}"
                "$<TARGET_PROPERTY:StemSepSILVER_VST3,JUCE_PLUGIN_ARTEFACT_FILE>/Contents/Resources"
        COMMENT "Copiando modelo e ONNX Runtime para o bundle")
endif()

# Ferramenta de teste (mesmo motor do plugin)
if(STEMSEP_BUILD_CLI)
    add_executable(stemsep_cli tools/cli/stemsep_cli.cpp ${ENGINE_SOURCES})
    target_include_directories(stemsep_cli PRIVATE
        third_party/pocketfft third_party/r8brain third_party/onnxruntime/include)
    if(MSVC)
        target_compile_options(stemsep_cli PRIVATE /utf-8 /bigobj)
    else()
        target_link_libraries(stemsep_cli PRIVATE ${CMAKE_DL_LIBS} pthread)
    endif()
endif()
