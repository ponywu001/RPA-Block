add_library(rpa-server STATIC
    src/ApiServer.cpp
    src/RunStore.cpp
    src/ScriptRepository.cpp
)

add_library(rpa::server ALIAS rpa-server)

target_include_directories(rpa-server PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(rpa-server PUBLIC rpa::core PRIVATE rpa_httplib)

rpa_set_target_defaults(rpa-server)
