# role: bootstrap the standalone insight-io backend build.
# revision: 2026-03-26 direct-session-slice
# major changes: adds the direct-session service, related tests, and keeps the
# focused standalone backend build aligned with docs/past-tasks.md.

include(FetchContent)
include(FindPkgConfig)

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG a7bc00e3307fecdb4d67545e93be7b88cfb1e186
)
FetchContent_MakeAvailable(httplib)

find_package(nlohmann_json 3 REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

set(INSIGHTIO_SCHEMA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/schema/001_initial.sql")

option(ENABLE_ORBBEC "Enable Orbbec discovery support" ON)
option(ENABLE_PIPEWIRE "Enable PipeWire discovery support" ON)

set(INSIGHTIO_HAS_ORBBEC OFF)
set(INSIGHTIO_HAS_PIPEWIRE OFF)

if (ENABLE_ORBBEC)
    set(INSIGHTIO_ORBBEC_SDK_ROOT
        "${CMAKE_SOURCE_DIR}/../insightos/third_party/orbbec_sdk/SDK"
        CACHE PATH "Orbbec SDK root")
    set(INSIGHTIO_ORBBEC_SDK_INCLUDE "${INSIGHTIO_ORBBEC_SDK_ROOT}/include")
    set(INSIGHTIO_ORBBEC_SDK_LIB "${INSIGHTIO_ORBBEC_SDK_ROOT}/lib")
    set(INSIGHTIO_ORBBEC_CONFIG_PATH
        "${INSIGHTIO_ORBBEC_SDK_ROOT}/config/OrbbecSDKConfig_v1.0.xml")

    if (EXISTS "${INSIGHTIO_ORBBEC_SDK_ROOT}")
        find_library(ORBBEC_SDK_LIB NAMES OrbbecSDK PATHS ${INSIGHTIO_ORBBEC_SDK_LIB} NO_DEFAULT_PATH)
        find_library(ORBBEC_USB_LIB NAMES ob_usb PATHS ${INSIGHTIO_ORBBEC_SDK_LIB} NO_DEFAULT_PATH)
        find_library(ORBBEC_DEPTH_ENGINE_LIB NAMES depthengine PATHS ${INSIGHTIO_ORBBEC_SDK_LIB} NO_DEFAULT_PATH)
        find_library(ORBBEC_LIVE555_LIB NAMES live555 PATHS ${INSIGHTIO_ORBBEC_SDK_LIB} NO_DEFAULT_PATH)

        if (ORBBEC_SDK_LIB AND ORBBEC_USB_LIB AND ORBBEC_DEPTH_ENGINE_LIB AND ORBBEC_LIVE555_LIB)
            set(INSIGHTIO_HAS_ORBBEC ON)
            message(STATUS "Orbbec SDK found at ${INSIGHTIO_ORBBEC_SDK_ROOT}")
        else()
            message(WARNING "Orbbec SDK libraries incomplete in ${INSIGHTIO_ORBBEC_SDK_LIB}")
        endif()
    else()
        message(WARNING "Orbbec SDK not found at ${INSIGHTIO_ORBBEC_SDK_ROOT}")
    endif()
endif()

if (ENABLE_PIPEWIRE)
    pkg_check_modules(PIPEWIRE QUIET libpipewire-0.3)
    if (PIPEWIRE_FOUND)
        set(INSIGHTIO_HAS_PIPEWIRE ON)
        message(STATUS "PipeWire found: ${PIPEWIRE_VERSION}")
    else()
        message(WARNING "PipeWire not found - PipeWire discovery disabled")
    endif()
endif()

set(INSIGHTIO_BACKEND_SOURCES
    src/schema_store.cpp
    src/types.cpp
    src/catalog.cpp
    src/session_service.cpp
    src/api/rest_server.cpp
    src/discovery/discovery.cpp
    src/discovery/v4l2_discovery.cpp
)

if (INSIGHTIO_HAS_ORBBEC)
    list(APPEND INSIGHTIO_BACKEND_SOURCES src/discovery/orbbec_discovery.cpp)
endif()

if (INSIGHTIO_HAS_PIPEWIRE)
    list(APPEND INSIGHTIO_BACKEND_SOURCES src/discovery/pipewire_discovery.cpp)
endif()

add_library(insightio_backend_support STATIC
    ${INSIGHTIO_BACKEND_SOURCES}
)

target_include_directories(insightio_backend_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        $<$<BOOL:${INSIGHTIO_HAS_ORBBEC}>:${INSIGHTIO_ORBBEC_SDK_INCLUDE}>
        $<$<BOOL:${INSIGHTIO_HAS_PIPEWIRE}>:${PIPEWIRE_INCLUDE_DIRS}>
)

target_compile_features(insightio_backend_support PUBLIC cxx_std_20)
target_compile_options(insightio_backend_support PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(insightio_backend_support
    PRIVATE
        INSIGHTIO_SCHEMA_PATH="${INSIGHTIO_SCHEMA_PATH}"
        $<$<BOOL:${INSIGHTIO_HAS_ORBBEC}>:INSIGHTIO_HAS_ORBBEC>
        $<$<BOOL:${INSIGHTIO_HAS_PIPEWIRE}>:INSIGHTIO_HAS_PIPEWIRE>
        $<$<BOOL:${INSIGHTIO_HAS_ORBBEC}>:INSIGHTIO_ORBBEC_CONFIG_PATH="${INSIGHTIO_ORBBEC_CONFIG_PATH}">
)

target_link_libraries(insightio_backend_support
    PUBLIC
        SQLite::SQLite3
        Threads::Threads
        httplib::httplib
        nlohmann_json::nlohmann_json
)

if (INSIGHTIO_HAS_ORBBEC)
    target_link_libraries(insightio_backend_support
        PUBLIC
            ${ORBBEC_SDK_LIB}
            ${ORBBEC_USB_LIB}
            ${ORBBEC_DEPTH_ENGINE_LIB}
            ${ORBBEC_LIVE555_LIB}
    )
endif()

if (INSIGHTIO_HAS_PIPEWIRE)
    target_link_libraries(insightio_backend_support PUBLIC ${PIPEWIRE_LIBRARIES})
endif()

add_executable(insightiod
    src/main.cpp
)

target_include_directories(insightiod PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(insightiod PRIVATE cxx_std_20)
target_compile_options(insightiod PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(insightiod PRIVATE insightio_backend_support)

if (INSIGHTIO_HAS_ORBBEC)
    set_target_properties(insightiod PROPERTIES BUILD_RPATH "${INSIGHTIO_ORBBEC_SDK_LIB}")
endif()

add_executable(schema_store_test
    tests/schema_store_test.cpp
)

target_include_directories(schema_store_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(schema_store_test PRIVATE cxx_std_20)
target_compile_options(schema_store_test PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(schema_store_test PRIVATE insightio_backend_support SQLite::SQLite3)

if (INSIGHTIO_HAS_ORBBEC)
    set_target_properties(schema_store_test PROPERTIES BUILD_RPATH "${INSIGHTIO_ORBBEC_SDK_LIB}")
endif()

add_executable(catalog_service_test
    tests/catalog_service_test.cpp
)

target_include_directories(catalog_service_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(catalog_service_test PRIVATE cxx_std_20)
target_compile_options(catalog_service_test PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(catalog_service_test PRIVATE insightio_backend_support)

if (INSIGHTIO_HAS_ORBBEC)
    set_target_properties(catalog_service_test PROPERTIES BUILD_RPATH "${INSIGHTIO_ORBBEC_SDK_LIB}")
endif()

add_executable(session_service_test
    tests/session_service_test.cpp
)

target_include_directories(session_service_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(session_service_test PRIVATE cxx_std_20)
target_compile_options(session_service_test PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(session_service_test PRIVATE insightio_backend_support SQLite::SQLite3)

if (INSIGHTIO_HAS_ORBBEC)
    set_target_properties(session_service_test PROPERTIES BUILD_RPATH "${INSIGHTIO_ORBBEC_SDK_LIB}")
endif()

add_executable(rest_server_test
    tests/rest_server_test.cpp
)

target_include_directories(rest_server_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(rest_server_test PRIVATE cxx_std_20)
target_compile_options(rest_server_test PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(rest_server_test PRIVATE insightio_backend_support)

if (INSIGHTIO_HAS_ORBBEC)
    set_target_properties(rest_server_test PROPERTIES BUILD_RPATH "${INSIGHTIO_ORBBEC_SDK_LIB}")
endif()

add_test(NAME schema_store_test COMMAND schema_store_test)
add_test(NAME catalog_service_test COMMAND catalog_service_test)
add_test(NAME session_service_test COMMAND session_service_test)
add_test(NAME rest_server_test COMMAND rest_server_test)
