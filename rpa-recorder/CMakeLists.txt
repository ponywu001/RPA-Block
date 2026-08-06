add_library(rpa-recorder STATIC
    src/Recorder.cpp
    src/RecordedEvent.cpp
    src/UiaInspector.cpp
)

add_library(rpa::recorder ALIAS rpa-recorder)

target_include_directories(rpa-recorder PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(rpa-recorder PUBLIC rpa::core)

if(WIN32)
    # uiautomationcore: element inspection. windowscodecs: PNG encoding.
    target_link_libraries(rpa-recorder PRIVATE
        uiautomationcore ole32 oleaut32 windowscodecs gdi32 user32 psapi)
endif()

rpa_set_target_defaults(rpa-recorder)
