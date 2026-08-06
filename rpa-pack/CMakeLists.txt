# The self-extracting launcher. Built as a GUI-subsystem exe so double-clicking
# the package never flashes a console window.
add_executable(rpa-pack-stub WIN32 stub.cpp)

# dist/RPA-Block.exe is this stub with the payload appended, so the icon has to
# be here for the packaged file to show it.
if(MSVC)
    target_sources(rpa-pack-stub PRIVATE stub.rc)
endif()

target_link_libraries(rpa-pack-stub PRIVATE shell32 ole32)

set_target_properties(rpa-pack-stub PROPERTIES
    WIN32_EXECUTABLE ON
    OUTPUT_NAME "rpa-pack-stub"
)

rpa_set_target_defaults(rpa-pack-stub)
