cmake_minimum_required(VERSION 3.10)

project(ClassicAPI LANGUAGES CXX)

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

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS} ${TINYCBOR_SOURCES})

# Dev-build sentinel — represents "newer than every plausible
# release". Encoded as v9999.99.99 = 9999 * 10000 + 99 * 100 + 99 =
# 99,999,999. Picked so addon-side feature gates of the form
# `CLASSIC_API_VERSION >= X` always pass during local development;
# CI release builds overwrite it via `-DCLASSICAPI_TAG=vX.Y.Z`.
set(CLASSICAPI_VERSION_VALUE 99999999)

set(CLASSICAPI_TAG
    ""
    CACHE STRING "Release tag like v1.2.3")

if(CLASSICAPI_TAG MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
  set(_MAJOR "${CMAKE_MATCH_1}")
  set(_MINOR "${CMAKE_MATCH_2}")
  set(_PATCH "${CMAKE_MATCH_3}")
  math(EXPR CLASSICAPI_VERSION_VALUE
       "${_MAJOR} * 10000 + ${_MINOR} * 100 + ${_PATCH}")
elseif(CLASSICAPI_TAG)
  message(
    FATAL_ERROR
      "Invalid CLASSICAPI_TAG: ${CLASSICAPI_TAG} (expected vMAJOR.MINOR.PATCH)"
  )
endif()

target_compile_definitions(
  ${PROJECT_NAME}
  PRIVATE CLASSICAPI_VERSION_VALUE=${CLASSICAPI_VERSION_VALUE})

target_link_libraries(${PROJECT_NAME} PRIVATE minhook advapi32)

target_include_directories(
  ${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                          external/minhook/include
                          external
                          external/tinycbor/src)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "ClassicAPI")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MT")
set(CMAKE_CXX_FLAGS_RELEASE "/MT /O2 /Ob2 /DNDEBUG")
set(CMAKE_SHARED_LINKER_FLAGS
    "/OPT:REF /OPT:ICF /MERGE:.rdata=.text /MERGE:.pdata=.text /SECTION:.text,EWR /NODEFAULTLIB:MSVCRT"
)
