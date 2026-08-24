cmake_minimum_required(VERSION 3.15)
project(SamVoiceSAPI VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SAM_ARCH "x64")
else()
    set(SAM_ARCH "x86")
endif()
message(STATUS "Building SAM SAPI5 for ${SAM_ARCH}")

# ---------------------------------------------------------------------------
# The synthesizer itself: portable C++, no Windows or COM dependency.
# ---------------------------------------------------------------------------
add_library(sam_engine STATIC
    src/engine/sam_tables.cpp
    src/engine/sam_reciter.cpp
    src/engine/sam_cmudict.cpp
    src/engine/sam_parser.cpp
    src/engine/sam_renderer.cpp
    src/engine/sam_engine.cpp
)
target_include_directories(sam_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/engine)
target_compile_definitions(sam_engine PRIVATE _CRT_SECURE_NO_WARNINGS)

# ---------------------------------------------------------------------------
# Shared Win32 support used by both the SAPI DLL and the settings utility.
# ---------------------------------------------------------------------------
add_library(sam_support STATIC
    src/sapi/sam_paths.cpp
    src/sapi/sam_log.cpp
    src/sapi/sam_settings.cpp
)
target_include_directories(sam_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/sapi)
target_link_libraries(sam_support PUBLIC sam_engine shell32 ole32)
target_compile_definitions(sam_support PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX UNICODE _UNICODE)

# ---------------------------------------------------------------------------
# The SAPI5 engine DLL. The same sources build for both architectures.
# ---------------------------------------------------------------------------
add_library(SamVoiceSAPI SHARED
    src/sapi/sapi_main.cpp
    src/sapi/com.cpp
    src/sapi/registry.cpp
    src/sapi/ISpDataKeyImpl.cpp
    src/sapi/IEnumSpObjectTokensImpl.cpp
    src/sapi/ISpTTSEngineImpl.cpp
    src/sapi/voice_token.cpp
    src/sapi/sam_sapi.def
)
target_link_libraries(SamVoiceSAPI PRIVATE
    sam_support
    ole32
    oleaut32
    advapi32
    shell32
)

# ---------------------------------------------------------------------------
# The accessible settings utility (64-bit build only; it is architecture
# independent and writes the same per-user settings file).
# ---------------------------------------------------------------------------
if(SAM_ARCH STREQUAL "x64")
    add_executable(SamVoiceSettings WIN32
        src/config/sam_config.cpp
        src/config/sam_config.rc
    )
    target_link_libraries(SamVoiceSettings PRIVATE
        sam_support
        comctl32
        winmm
        ole32
        shell32
    )
    target_include_directories(SamVoiceSettings PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/config)
    # The .rc embeds our own manifest (common controls v6, DPI awareness), so
    # stop the linker from generating a second one.
    target_link_options(SamVoiceSettings PRIVATE "/MANIFEST:NO")

    # Command-line renderer, used by the verification scripts.
    add_executable(sam_render tools/sam_render.cpp)
    target_link_libraries(sam_render PRIVATE sam_engine)
    target_compile_definitions(sam_render PRIVATE _CRT_SECURE_NO_WARNINGS)

    # Settings round-trip test.
    add_executable(sam_settings_test tools/sam_settings_test.cpp)
    target_link_libraries(sam_settings_test PRIVATE sam_support)
endif()

if(MSVC)
    foreach(target sam_engine sam_support SamVoiceSAPI)
        target_compile_options(${target} PRIVATE /W3 /MP)
        set_property(TARGET ${target} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endforeach()
    if(SAM_ARCH STREQUAL "x64")
        foreach(target SamVoiceSettings sam_render sam_settings_test)
            target_compile_options(${target} PRIVATE /W3 /MP)
            set_property(TARGET ${target} PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endforeach()
    endif()
endif()
