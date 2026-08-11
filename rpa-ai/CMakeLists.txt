# The schema and prompt text need nothing but rpa-core and JSON, so they live in
# their own target. That keeps the output_schema contract — the riskiest part of
# the AI integration — testable on a machine with no Qt installed.
add_library(rpa-ai-schema STATIC
    src/OutputSchema.cpp
    src/PromptBuilder.cpp
    src/SseParser.cpp
    src/ReplyDecoder.cpp
    src/DraftRepair.cpp
)

add_library(rpa::ai_schema ALIAS rpa-ai-schema)

target_include_directories(rpa-ai-schema PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(rpa-ai-schema PUBLIC rpa::core)
rpa_set_target_defaults(rpa-ai-schema)

# The gateway client needs Qt for its network stack and signals.
if(RPA_BUILD_STUDIO)
    # The header must be listed explicitly: AUTOMOC finds a Q_OBJECT header on
    # its own only when it sits next to the .cpp of the same name, and this one
    # lives under include/rpa/ai/ while the source is in src/. Without it moc
    # never runs and every signal fails to link.
    add_library(rpa-ai STATIC
        src/AgentClient.cpp
        include/rpa/ai/AgentClient.h
    )
    add_library(rpa::ai ALIAS rpa-ai)

    target_include_directories(rpa-ai PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    target_link_libraries(rpa-ai PUBLIC rpa::ai_schema Qt6::Core Qt6::Network)
    set_target_properties(rpa-ai PROPERTIES AUTOMOC ON)
    rpa_set_target_defaults(rpa-ai)

    # Console probe. The SSE framing can only be held to what the gateway really
    # sends by talking to it, and that needs no GUI -- so it gets its own binary
    # rather than a hidden mode of the desktop app.
    add_executable(rpa-ai-probe tools/probe_main.cpp)
    target_link_libraries(rpa-ai-probe PRIVATE rpa::ai rpa::core)
    rpa_set_target_defaults(rpa-ai-probe)
endif()
