cmake_minimum_required(VERSION 3.10)

project(ClassicAPI LANGUAGES CXX RC)

# WoW.exe is x86, so the injected DLL must be x86 too — there is no valid
# 64-bit configure of this project. Fail here rather than let one through:
# a 64-bit build's first visible symptom is a static_assert deep in a
# struct-layout check, which reads as an unrelated code fault. An IDE that
# configures on its own (VS Code's CMake Tools picks an architecture from a
# scanned kit) can flip an existing build directory this way, which is what
# CMakePresets.json's `win32` preset exists to prevent.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "ClassicAPI builds 32-bit only. This configure uses "
        "${CMAKE_SIZEOF_VOID_P}-byte pointers. Delete the build directory, "
        "because CMake cannot switch platform in place. Then configure with "
        "`cmake --preset win32` or `cmake -B build -A Win32`.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# minhook's CMakeLists.txt declares `cmake_minimum_required(VERSION 3.0...3.5)`,
# which CMake 4.x flags as a deprecation warning every reconfigure. Bumping
# the minimum policy version here promotes its policy set to 3.10 without
# patching the vendored submodule.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
add_subdirectory(external/minhook)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "src/*.h")

set(TINYCBOR_SOURCES
    external/tinycbor/src/cborencoder.c
    external/tinycbor/src/cborencoder_close_container_checked.c
    external/tinycbor/src/cborencoder_float.c
    external/tinycbor/src/cborparser.c
    external/tinycbor/src/cborparser_dup_string.c
    external/tinycbor/src/cborparser_float.c
    external/tinycbor/src/cborerrorstrings.c)

# Version derivation. Dev builds use a sentinel "newer than every
# plausible release" so addon-side `CLASSIC_API_VERSION >= X` gates
# always pass locally. CI release builds pass `-DCLASSICAPI_TAG=vX.Y.Z`,
# which drives BOTH the compiled `CLASSIC_API_VERSION` int AND the
# embedded addon's `## Version:` toc line (substituted in
# embed_addon.cmake). The on-disk source toc stays `## Version: DEV`;
# only the embedded copy is stamped, so a release build needs nothing
# but the flag — no source edit.
set(CLASSICAPI_VERSION_VALUE 99999999)
set(CLASSICAPI_VER_MAJOR 9999)
set(CLASSICAPI_VER_MINOR 99)
set(CLASSICAPI_VER_PATCH 99)
set(CLASSICAPI_TOC_VERSION "DEV")

set(CLASSICAPI_TAG
    ""
    CACHE STRING "Release tag like v1.2.3")

if(CLASSICAPI_TAG MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
  set(CLASSICAPI_VER_MAJOR "${CMAKE_MATCH_1}")
  set(CLASSICAPI_VER_MINOR "${CMAKE_MATCH_2}")
  set(CLASSICAPI_VER_PATCH "${CMAKE_MATCH_3}")
  math(EXPR CLASSICAPI_VERSION_VALUE
       "${CLASSICAPI_VER_MAJOR} * 10000 + ${CLASSICAPI_VER_MINOR} * 100 + ${CLASSICAPI_VER_PATCH}")
  set(CLASSICAPI_TOC_VERSION
      "${CLASSICAPI_VER_MAJOR}.${CLASSICAPI_VER_MINOR}.${CLASSICAPI_VER_PATCH}")
elseif(CLASSICAPI_TAG)
  message(
    FATAL_ERROR
      "Invalid CLASSICAPI_TAG: ${CLASSICAPI_TAG} (expected vMAJOR.MINOR.PATCH)"
  )
endif()

# Bundle `AddOns/!!!ClassicAPI/` into a generated header so the DLL can
# surface the addon to the engine even when the user doesn't have it
# installed on disk. The file-read hook in `src/addons/Embedded.cpp`
# returns these buffers when the engine asks for paths under
# `Interface\AddOns\!!!ClassicAPI\`.
set(EMBEDDED_CLASSICAPI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/AddOns/!!!ClassicAPI")
set(EMBEDDED_CLASSICAPI_HEADER "${CMAKE_BINARY_DIR}/generated/embedded_classicapi.h")
file(GLOB_RECURSE EMBEDDED_CLASSICAPI_FILES CONFIGURE_DEPENDS
     "${EMBEDDED_CLASSICAPI_SRC}/*")
# The `.classicapi-dev` marker is a disk-only developer signal — keep it
# out of the embed inputs (embed_addon.cmake filters it too).
list(FILTER EMBEDDED_CLASSICAPI_FILES EXCLUDE REGEX "/\\.classicapi-dev$")
add_custom_command(
    OUTPUT "${EMBEDDED_CLASSICAPI_HEADER}"
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR=${EMBEDDED_CLASSICAPI_SRC}
        -DOUT_HEADER=${EMBEDDED_CLASSICAPI_HEADER}
        -DADDON_NAME=ClassicAPI
        -DADDON_VERSION=${CLASSICAPI_TOC_VERSION}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_addon.cmake"
    DEPENDS
        ${EMBEDDED_CLASSICAPI_FILES}
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_addon.cmake"
    COMMENT "Embedding !!!ClassicAPI addon into ${EMBEDDED_CLASSICAPI_HEADER}"
    VERBATIM)

# Bundle `src/bindings/Bindings.xml` (our TARGETING-group additions)
# into a generated header. `src/bindings/Inject.cpp` extracts the
# inner `<Binding>` elements from the embedded buffer and splices them
# into the engine's `Interface\FrameXML\Bindings.xml` at load time.
set(EMBEDDED_BINDINGS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/bindings/Bindings.xml")
set(EMBEDDED_BINDINGS_HEADER "${CMAKE_BINARY_DIR}/generated/embedded_bindings.h")
add_custom_command(
    OUTPUT "${EMBEDDED_BINDINGS_HEADER}"
    COMMAND ${CMAKE_COMMAND}
        -DINPUT_FILE=${EMBEDDED_BINDINGS_SRC}
        -DOUT_HEADER=${EMBEDDED_BINDINGS_HEADER}
        -DSYMBOL=kEmbeddedBindingsXml
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake"
    DEPENDS
        "${EMBEDDED_BINDINGS_SRC}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake"
    COMMENT "Embedding bindings XML into ${EMBEDDED_BINDINGS_HEADER}"
    VERBATIM)

add_library(${PROJECT_NAME} SHARED
    ${SOURCES} ${HEADERS} ${TINYCBOR_SOURCES}
    ${EMBEDDED_CLASSICAPI_HEADER}
    ${EMBEDDED_BINDINGS_HEADER})

target_compile_definitions(
  ${PROJECT_NAME}
  PRIVATE CLASSICAPI_VERSION_VALUE=${CLASSICAPI_VERSION_VALUE})

# Stamp the PE VERSIONINFO resource from the same version components, so the
# DLL's file/product version (Explorer -> Properties -> Details) matches the
# CLASSIC_API_VERSION Lua global. The .rsrc section the resource lands in is
# untouched by the .rdata/.pdata section merges below.
set(CLASSICAPI_VER_STRING
    "${CLASSICAPI_VER_MAJOR}.${CLASSICAPI_VER_MINOR}.${CLASSICAPI_VER_PATCH}.0")
set(CLASSICAPI_VERSION_RC "${CMAKE_BINARY_DIR}/generated/version.rc")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in"
    "${CLASSICAPI_VERSION_RC}"
    @ONLY)
target_sources(${PROJECT_NAME} PRIVATE "${CLASSICAPI_VERSION_RC}")

# ole32: COM (CoInitializeEx / CoCreateInstance / CoTaskMemFree) for the
# SAPI text-to-speech backend in src/voicechat/Tts.cpp. The SAPI GUIDs are
# instantiated in that TU via <initguid.h>, so no sapi.lib is needed.
# user32: Win32 clipboard API (OpenClipboard / SetClipboardData) for
# CopyToClipboard in src/clipboard/Copy.cpp.
target_link_libraries(${PROJECT_NAME} PRIVATE minhook advapi32 ole32 user32)

target_include_directories(
  ${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                          ${CMAKE_BINARY_DIR}/generated
                          external/minhook/include
                          external
                          external/tinycbor/src)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "ClassicAPI")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MT")
# /Zi retains full debug info (source file + line numbers, locals, types) in a
# separate PDB WITHOUT disabling optimization — the DLL stays a fully optimized
# /O2 release build; the symbols just live in ClassicAPI.pdb on the side. Feeds
# the linker's /DEBUG below.
set(CMAKE_CXX_FLAGS_RELEASE "/MT /O2 /Ob2 /DNDEBUG /Zi")
set(CMAKE_SHARED_LINKER_FLAGS
    # Two debug artifacts for triaging crashes from a SHIPPED DLL. Both are
    # published as release artifacts, match this exact build, and have no effect
    # on code layout:
    #   /MAP   -> build/Release/ClassicAPI.map: a text symbol->address table
    #             that resolves the `0001:offset` addresses in WoW's ERROR #132
    #             crash log back to functions by hand (no debugger/dump needed).
    #   /DEBUG -> build/Release/ClassicAPI.pdb: full symbols (paired with the
    #             compiler's /Zi) so WinDbg/x64dbg/VS produce a line-level
    #             symbolicated call stack from a crash dump. A SEPARATE file —
    #             it is NOT embedded in the DLL, only a small signature record
    #             linking the two is.
    # /DEBUG implicitly disables /OPT:REF and /OPT:ICF (dead-code strip +
    # identical-COMDAT fold); they are listed explicitly below so they stay ON
    # and the shipped DLL does not grow. (/OPT:ICF folds identical functions, so
    # a few map symbols share an address — harmless.)
    # /DEF exports `Load` (VanillaFixes' post-injection entry point) undecorated
    # so its GetProcAddress(module, "Load") resolves. See src/ClassicAPI.def and
    # the loader-lock rationale in src/DllMain.cpp.
    "/OPT:REF /OPT:ICF /MERGE:.rdata=.text /MERGE:.pdata=.text /SECTION:.text,EWR /NODEFAULTLIB:MSVCRT /MAP /DEBUG /DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/ClassicAPI.def"
)
