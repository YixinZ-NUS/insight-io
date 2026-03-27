# role: build task-9 example apps for live route-oriented SDK validation.
# revision: 2026-03-27 task9-sdk-route-surface
# major changes: adds latency, grouped Orbbec overlay, and mixed-device route
# callback examples on top of the new `insightos::App` SDK.

find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs)

function(insightio_add_example target)
    add_executable(${target} ${ARGN})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(${target} PRIVATE insightos_sdk)
    if (INSIGHTIO_HAS_ORBBEC)
        set_target_properties(${target} PROPERTIES BUILD_RPATH "${INSIGHTIO_ORBBEC_SDK_LIB}")
    endif()
endfunction()

insightio_add_example(v4l2_latency_monitor
    v4l2_latency_monitor.cpp
)

insightio_add_example(mixed_device_consumer
    mixed_device_consumer.cpp
)

if (OpenCV_FOUND)
    insightio_add_example(orbbec_depth_overlay
        orbbec_depth_overlay.cpp
    )
    target_link_libraries(orbbec_depth_overlay PRIVATE ${OpenCV_LIBS})
else()
    message(WARNING "OpenCV not found - skipping orbbec_depth_overlay example")
endif()
