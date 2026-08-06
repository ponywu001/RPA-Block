add_executable(rpa-cli main.cpp)

target_link_libraries(rpa-cli PRIVATE rpa::core)

# CommandLineToArgvW, for reading non-ASCII arguments losslessly.
if(WIN32)
    target_link_libraries(rpa-cli PRIVATE shell32)
endif()

# `serve` only exists when the REST module is part of the build.
if(RPA_BUILD_SERVER)
    target_link_libraries(rpa-cli PRIVATE rpa::server rpa_httplib)
    target_compile_definitions(rpa-cli PRIVATE RPA_CLI_WITH_SERVER)
endif()

# Likewise `ocr`, which needs the OCR engine and OpenCV's image codecs.
if(RPA_BUILD_VISION)
    target_link_libraries(rpa-cli PRIVATE rpa::vision)
    target_compile_definitions(rpa-cli PRIVATE RPA_CLI_WITH_VISION)
endif()

rpa_set_target_defaults(rpa-cli)
