qt_add_executable(rpa-studio
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/AppSettings.cpp
    src/AppSettings.h
    src/AuthorGate.cpp
    src/AuthorGate.h
    src/ExecutionController.cpp
    src/ExecutionController.h
    src/FlowCanvas.cpp
    src/FlowCanvas.h
    src/FlowVariablesDialog.cpp
    src/FlowVariablesDialog.h
    src/BlockPalette.cpp
    src/BlockPalette.h
    src/StandardButtons.cpp
    src/StandardButtons.h
    src/Theme.cpp
    src/Theme.h
    src/BlockStyle.cpp
    src/BlockStyle.h
    src/PropertyPanel.cpp
    src/PropertyPanel.h
    src/ChatDock.cpp
    src/ChatDock.h
    src/TargetPickerOverlay.cpp
    src/TargetPickerOverlay.h
    src/ScreenGeometryDump.cpp
    src/ScreenGeometryDump.h
    src/UiaTreeDump.cpp
    src/UiaTreeDump.h
    src/CommandPreviewDialog.cpp
    src/CommandPreviewDialog.h
    src/ApiPanelDialog.cpp
    src/ApiPanelDialog.h
    src/TunnelController.cpp
    src/TunnelController.h
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    src/RecordingDialogs.cpp
    src/RecordingDialogs.h
    resources/app.qrc
)

# The .rc gives the executable file itself an icon, which setWindowIcon cannot do
# — that only reaches windows this process creates, not Explorer or a pinned
# shortcut. MSVC compiles .rc sources directly.
if(MSVC)
    target_sources(rpa-studio PRIVATE resources/app.rc)
endif()

target_include_directories(rpa-studio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(rpa-studio PRIVATE
    rpa::core
    rpa::ai
    rpa::vision
    rpa::server
    rpa_httplib
    Qt6::Widgets
    Qt6::Network
    Qt6::Gui
)

# The recorder is Windows-only; the studio builds without it elsewhere, minus
# the record button's backing implementation.
if(TARGET rpa-recorder)
    target_link_libraries(rpa-studio PRIVATE rpa::recorder)
endif()

if(WIN32)
    # Credential Manager, for the AI gateway secret and the tunnel authtoken.
    target_link_libraries(rpa-studio PRIVATE advapi32)
endif()

set_target_properties(rpa-studio PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    WIN32_EXECUTABLE ON
)

rpa_set_target_defaults(rpa-studio)

# Make the build output directly runnable: the Qt runtime plus the two non-Qt
# DLLs windeployqt knows nothing about. Without this the exe starts and then dies
# with a missing-DLL dialog the moment it touches OCR or template matching.
if(WIN32)
    find_program(RPA_WINDEPLOYQT windeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(RPA_WINDEPLOYQT)
        add_custom_command(TARGET rpa-studio POST_BUILD
            COMMAND "${RPA_WINDEPLOYQT}" --no-translations --no-system-d3d-compiler
                    --no-compiler-runtime "$<TARGET_FILE:rpa-studio>"
            COMMENT "Deploying the Qt runtime alongside rpa-studio"
        )
    else()
        message(WARNING
            "windeployqt was not found, so the build output will not be runnable "
            "without Qt on PATH. Set RPA_WINDEPLOYQT to its path.")
    endif()

    # OpenCV: the prebuilt Windows package ships one "world" DLL. Resolve it from
    # the same tree OpenCV_DIR points at rather than hardcoding a version.
    file(GLOB RPA_OPENCV_DLLS "${OpenCV_DIR}/x64/vc*/bin/opencv_world*.dll")
    list(FILTER RPA_OPENCV_DLLS EXCLUDE REGEX "[0-9]d\\.dll$")  # drop the debug build
    if(RPA_OPENCV_DLLS)
        add_custom_command(TARGET rpa-studio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RPA_OPENCV_DLLS}
                    "$<TARGET_FILE_DIR:rpa-studio>"
            COMMENT "Copying the OpenCV runtime")
    else()
        message(WARNING "Could not locate opencv_world*.dll under ${OpenCV_DIR}; "
                        "copy it beside rpa-studio.exe manually.")
    endif()

    # The MSVC runtime, deployed beside the executable.
    #
    # windeployqt is invoked with --no-compiler-runtime, so nothing else brings
    # these along. That is invisible here -- a machine with the build tools
    # installed already has them on the system -- and fatal on a customer's,
    # where the app dies at startup with a missing-DLL dialog before any of our
    # code runs. App-local deployment is permitted for the redistributable and
    # avoids making the installer chain a second setup program.
    set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
    include(InstallRequiredSystemLibraries)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        add_custom_command(TARGET rpa-studio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} "$<TARGET_FILE_DIR:rpa-studio>"
            COMMENT "Copying the MSVC runtime")
    else()
        message(WARNING
            "Could not locate the MSVC redistributable DLLs; the build output will "
            "only start on machines that already have them installed.")
    endif()

    # ONNX Runtime sits next to the import library it was found through.
    get_filename_component(RPA_ORT_LIB_DIR "${ONNXRUNTIME_LIBRARY}" DIRECTORY)
    file(GLOB RPA_ORT_DLLS "${RPA_ORT_LIB_DIR}/onnxruntime*.dll")
    if(RPA_ORT_DLLS)
        add_custom_command(TARGET rpa-studio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RPA_ORT_DLLS}
                    "$<TARGET_FILE_DIR:rpa-studio>"
            COMMENT "Copying the ONNX Runtime")
    else()
        message(WARNING "Could not locate onnxruntime*.dll next to ${ONNXRUNTIME_LIBRARY}; "
                        "copy it beside rpa-studio.exe manually.")
    endif()
endif()
