set(RPA_TEST_SOURCES
    main.cpp
    test_script_io.cpp
    test_variables.cpp
    test_executor.cpp
    test_geometry.cpp
    test_keys.cpp
    test_output_schema.cpp
    test_text_match.cpp
    test_sse_parser.cpp
    test_reply_decoder.cpp
    test_draft_repair.cpp
)

# The server tests boot a real HTTP listener, so they only build alongside it.
if(RPA_BUILD_SERVER)
    list(APPEND RPA_TEST_SOURCES test_server.cpp)
endif()

add_executable(rpa-tests ${RPA_TEST_SOURCES})

target_include_directories(rpa-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rpa-tests PRIVATE rpa::core rpa::ai_schema)

# Recorded gateway responses live here. They are what holds the SSE framing to
# what the service really sends rather than to what this client assumes.
target_compile_definitions(rpa-tests
    PRIVATE RPA_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

if(RPA_BUILD_SERVER)
    target_link_libraries(rpa-tests PRIVATE rpa::server rpa_httplib)
endif()

rpa_set_target_defaults(rpa-tests)

add_test(NAME rpa-tests COMMAND rpa-tests)
