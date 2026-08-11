add_library(rpa-core STATIC
    src/Script.cpp
    src/ScriptIO.cpp
    src/Variables.cpp
    src/Executor.cpp
    src/Geometry.cpp
    src/TextMatch.cpp
    src/InputWin32.cpp
    src/HttpWin32.cpp
    src/ProcessWin32.cpp
)

add_library(rpa::core ALIAS rpa-core)

target_include_directories(rpa-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(rpa-core PUBLIC rpa_json)

if(WIN32)
    target_link_libraries(rpa-core PRIVATE winhttp user32 shell32 ole32)
endif()

rpa_set_target_defaults(rpa-core)
