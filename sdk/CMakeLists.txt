# role: build the task-9 route-oriented SDK and its focused tests.
# revision: 2026-03-27 task9-sdk-route-surface
# major changes: adds the high-level `insightos::App` library on top of the
# checked-in REST and IPC runtime contracts plus focused synthetic callback
# integration coverage.

add_library(insightos_sdk STATIC
    src/app.cpp
)

target_include_directories(insightos_sdk
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(insightos_sdk PUBLIC cxx_std_20)
target_compile_options(insightos_sdk PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(insightos_sdk
    PUBLIC
        insightio_backend_support
)

add_executable(app_sdk_test
    tests/app_sdk_test.cpp
)

target_include_directories(app_sdk_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/backend/include
)
target_compile_features(app_sdk_test PRIVATE cxx_std_20)
target_compile_options(app_sdk_test PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(app_sdk_test PRIVATE insightos_sdk)

if (INSIGHTIO_HAS_ORBBEC)
    set_target_properties(app_sdk_test PROPERTIES BUILD_RPATH "${INSIGHTIO_ORBBEC_SDK_LIB}")
endif()

add_test(NAME app_sdk_test COMMAND app_sdk_test)
