cmake_minimum_required(VERSION 3.24)

project(RPA-Block
    VERSION 0.1.0
    DESCRIPTION "C++ RPA low-code authoring tool with OCR/template targeting, AI flow generation, and a REST API"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# ---------------------------------------------------------------------------
# Options. Modules that need heavyweight third-party SDKs auto-disable when
# those SDKs are absent, so a bare checkout still builds the core and its tests.
# ---------------------------------------------------------------------------
option(RPA_BUILD_TESTS   "Build unit tests"                          ON)
option(RPA_BUILD_CLI     "Build the headless validate/run/serve tool" ON)
option(RPA_BUILD_SERVER  "Build the embedded REST API server"        ON)
option(RPA_BUILD_VISION  "Build the OCR / template-matching module"  OFF)
option(RPA_BUILD_RECORDER "Build the input recorder"                 ON)
option(RPA_BUILD_STUDIO  "Build the Qt 6 desktop application"        OFF)

# The desktop app takes screenshots, resolves OCR/template targets, and serves
# the REST API, so those modules are not optional once it is enabled.
if(RPA_BUILD_STUDIO)
    if(NOT RPA_BUILD_VISION)
        message(STATUS "RPA_BUILD_STUDIO implies RPA_BUILD_VISION; enabling it.")
        set(RPA_BUILD_VISION ON CACHE BOOL
            "Build the OCR / template-matching module" FORCE)
    endif()
    if(NOT RPA_BUILD_SERVER)
        message(STATUS "RPA_BUILD_STUDIO implies RPA_BUILD_SERVER; enabling it.")
        set(RPA_BUILD_SERVER ON CACHE BOOL
            "Build the embedded REST API server" FORCE)
    endif()
    # MainWindow uses the Recorder unconditionally, so leaving it off would fail
    # at link rather than degrading gracefully.
    if(WIN32 AND NOT RPA_BUILD_RECORDER)
        message(STATUS "RPA_BUILD_STUDIO implies RPA_BUILD_RECORDER; enabling it.")
        set(RPA_BUILD_RECORDER ON CACHE BOOL "Build the input recorder" FORCE)
    endif()
endif()

# ---------------------------------------------------------------------------
# Shared compile settings
# ---------------------------------------------------------------------------
function(rpa_set_target_defaults target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
        target_compile_definitions(${target} PRIVATE
            UNICODE _UNICODE
            WIN32_LEAN_AND_MEAN NOMINMAX
            _CRT_SECURE_NO_WARNINGS
        )
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Header-only third-party libraries, vendored under third_party/ so the build
# works offline.
# ---------------------------------------------------------------------------
add_library(rpa_json INTERFACE)
target_include_directories(rpa_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/include)

add_library(rpa_httplib INTERFACE)
target_include_directories(rpa_httplib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
if(WIN32)
    target_link_libraries(rpa_httplib INTERFACE ws2_32 crypt32 cryptui)
endif()

# ---------------------------------------------------------------------------
# Dependency probes for the optional modules
# ---------------------------------------------------------------------------
if(RPA_BUILD_VISION)
    # Turning vision off is only advice the user can act on when the studio did
    # not force it on for them.
    if(RPA_BUILD_STUDIO)
        set(RPA_VISION_OPT_OUT "turn the desktop app off with -DRPA_BUILD_STUDIO=OFF")
    else()
        set(RPA_VISION_OPT_OUT "build without the vision module using -DRPA_BUILD_VISION=OFF")
    endif()

    find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs)
    if(NOT OpenCV_FOUND)
        message(FATAL_ERROR
            "RPA_BUILD_VISION=ON but OpenCV was not found.\n"
            "  Point CMake at it with -DOpenCV_DIR=<prefix>/lib/cmake/opencv4 or a vcpkg "
            "toolchain file, or ${RPA_VISION_OPT_OUT} (see README.md).")
    endif()

    find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
        PATH_SUFFIXES onnxruntime onnxruntime/core/session)
    find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime)
    if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY)
        message(FATAL_ERROR
            "RPA_BUILD_VISION=ON but ONNX Runtime was not found.\n"
            "  Set -DONNXRUNTIME_INCLUDE_DIR=<dir containing onnxruntime_cxx_api.h> and "
            "-DONNXRUNTIME_LIBRARY=<path to onnxruntime.lib>, or ${RPA_VISION_OPT_OUT} "
            "(see README.md).")
    endif()
endif()

if(RPA_BUILD_STUDIO)
    # 6.3 is the floor: MainWindow uses the QWidget::addAction(text, shortcut,
    # receiver, slot) overload introduced there.
    find_package(Qt6 6.3 QUIET COMPONENTS Widgets Network Gui)
    if(NOT Qt6_FOUND)
        message(FATAL_ERROR
            "RPA_BUILD_STUDIO=ON but Qt 6.3 or newer was not found.\n"
            "  Point CMake at it with -DCMAKE_PREFIX_PATH=C:/Qt/6.x.y/msvc2022_64, or build "
            "without the desktop app using -DRPA_BUILD_STUDIO=OFF (see README.md).")
    endif()
endif()

if(RPA_BUILD_STUDIO)
    # Already located above; this pulls in the qt_* CMake API for real.
    find_package(Qt6 6.3 REQUIRED COMPONENTS Widgets Network Gui)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
endif()

# ---------------------------------------------------------------------------
# Modules
# ---------------------------------------------------------------------------
add_subdirectory(rpa-core)

# Always added: it defines the Qt-free schema target, and gates the Qt client on
# RPA_BUILD_STUDIO internally.
add_subdirectory(rpa-ai)

if(RPA_BUILD_VISION)
    add_subdirectory(rpa-vision)
endif()

if(RPA_BUILD_RECORDER AND WIN32)
    add_subdirectory(rpa-recorder)
endif()

if(RPA_BUILD_SERVER)
    add_subdirectory(rpa-server)
endif()

if(RPA_BUILD_STUDIO)
    add_subdirectory(rpa-studio)
endif()

if(RPA_BUILD_CLI)
    add_subdirectory(rpa-cli)
endif()

# The single-file packager only makes sense once there is a desktop app to wrap.
if(RPA_BUILD_STUDIO AND WIN32)
    add_subdirectory(rpa-pack)
endif()

if(RPA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)

    # Guard against an example flow rotting: CI fails if one stops validating.
    if(RPA_BUILD_CLI)
        file(GLOB RPA_EXAMPLE_FLOWS "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.rpa.json")
        if(RPA_EXAMPLE_FLOWS)
            add_test(NAME example-flows-validate
                     COMMAND rpa-cli validate ${RPA_EXAMPLE_FLOWS})
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Configuration summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "RPA-Block ${PROJECT_VERSION}")
message(STATUS "  rpa-core      : ON (always)")
message(STATUS "  rpa-vision    : ${RPA_BUILD_VISION}")
message(STATUS "  rpa-recorder  : ${RPA_BUILD_RECORDER}")
message(STATUS "  rpa-server    : ${RPA_BUILD_SERVER}")
message(STATUS "  rpa-studio    : ${RPA_BUILD_STUDIO} (implies rpa-ai)")
message(STATUS "  rpa-cli       : ${RPA_BUILD_CLI}")
message(STATUS "  tests         : ${RPA_BUILD_TESTS}")
message(STATUS "")
