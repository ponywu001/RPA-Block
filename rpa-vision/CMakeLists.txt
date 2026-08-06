add_library(rpa-vision STATIC
    src/ScreenCapture.cpp
    src/OcrEngine.cpp
    src/TemplateMatcher.cpp
    src/VisionLocator.cpp
)

add_library(rpa::vision ALIAS rpa-vision)

target_include_directories(rpa-vision
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${ONNXRUNTIME_INCLUDE_DIR}
)

target_link_libraries(rpa-vision
    PUBLIC  rpa::core ${OpenCV_LIBS}
    PRIVATE ${ONNXRUNTIME_LIBRARY}
)

if(WIN32)
    target_link_libraries(rpa-vision PRIVATE gdi32 user32)
endif()

rpa_set_target_defaults(rpa-vision)
