include(FindPkgConfig)
include(FetchContent)

# ── cpp-httplib (header-only HTTP library) — pinned SHA for supply-chain safety
# Donor reference: iocontroller-insightos/CMakeLists.txt (commit 4032eb4)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        a7bc00e3307fecdb4d67545e93be7b88cfb1e186  # v0.18.3
)
FetchContent_MakeAvailable(httplib)

# ── nlohmann_json (installed via apt: nlohmann-json3-dev) ────────────────
find_package(nlohmann_json 3 REQUIRED)

# ── Threads (needed by httplib + signal handling) ────────────────────────
find_package(Threads REQUIRED)

# ── SQLite3 (device/session store) ───────────────────────────────────────
find_package(SQLite3 REQUIRED)

# ── Optional backends ────────────────────────────────────────────────────

option(ENABLE_ORBBEC   "Enable Orbbec depth-camera support" ON)
option(ENABLE_PIPEWIRE "Enable PipeWire audio support"      ON)

set(INSIGHTOS_HAS_ORBBEC   OFF)
set(INSIGHTOS_HAS_PIPEWIRE OFF)

# ── Orbbec SDK (vendored under third_party/orbbec_sdk) ─────────────────────

if (ENABLE_ORBBEC)
    set(INSIGHTOS_ORBBEC_SDK_ROOT
        "${CMAKE_SOURCE_DIR}/third_party/orbbec_sdk/SDK"
        CACHE PATH "Orbbec SDK root")
    set(INSIGHTOS_ORBBEC_SDK_INCLUDE "${INSIGHTOS_ORBBEC_SDK_ROOT}/include")
    set(INSIGHTOS_ORBBEC_SDK_LIB     "${INSIGHTOS_ORBBEC_SDK_ROOT}/lib")

    if (EXISTS "${INSIGHTOS_ORBBEC_SDK_ROOT}")
        find_library(ORBBEC_SDK_LIB          NAMES OrbbecSDK   PATHS ${INSIGHTOS_ORBBEC_SDK_LIB} NO_DEFAULT_PATH)
        find_library(ORBBEC_USB_LIB          NAMES ob_usb      PATHS ${INSIGHTOS_ORBBEC_SDK_LIB} NO_DEFAULT_PATH)
        find_library(ORBBEC_DEPTH_ENGINE_LIB NAMES depthengine PATHS ${INSIGHTOS_ORBBEC_SDK_LIB} NO_DEFAULT_PATH)
        find_library(ORBBEC_LIVE555_LIB      NAMES live555     PATHS ${INSIGHTOS_ORBBEC_SDK_LIB} NO_DEFAULT_PATH)

        if (ORBBEC_SDK_LIB AND ORBBEC_USB_LIB AND ORBBEC_DEPTH_ENGINE_LIB AND ORBBEC_LIVE555_LIB)
            set(INSIGHTOS_HAS_ORBBEC ON)
            message(STATUS "Orbbec SDK found at ${INSIGHTOS_ORBBEC_SDK_ROOT}")
        else()
            message(WARNING "Orbbec SDK libraries incomplete in ${INSIGHTOS_ORBBEC_SDK_LIB}")
        endif()
    else()
        message(WARNING "Orbbec SDK not found at ${INSIGHTOS_ORBBEC_SDK_ROOT}")
    endif()
endif()

# ── PipeWire (optional) ──────────────────────────────────────────────────

if (ENABLE_PIPEWIRE)
    pkg_check_modules(PIPEWIRE QUIET libpipewire-0.3)
    if (PIPEWIRE_FOUND)
        set(INSIGHTOS_HAS_PIPEWIRE ON)
        message(STATUS "PipeWire found: ${PIPEWIRE_VERSION}")
    else()
        message(WARNING "PipeWire not found — audio discovery disabled")
    endif()
endif()

# ── Source files ─────────────────────────────────────────────────────────

set(INSIGHTOS_SOURCES
    src/main.cpp
    src/types.cpp
    src/catalog.cpp
    src/device_store.cpp
    src/session_manager.cpp
    src/api/rest_server.cpp
    src/discovery/discovery.cpp
    src/discovery/v4l2_discovery.cpp
    src/ipc/control_server.cpp
    src/ipc/channel_registry.cpp
    src/workers/synthetic_worker.cpp
    src/workers/v4l2_worker.cpp
)

if (INSIGHTOS_HAS_ORBBEC)
    list(APPEND INSIGHTOS_SOURCES
        src/discovery/orbbec_discovery.cpp
        src/workers/orbbec_worker.cpp
    )
endif()
if (INSIGHTOS_HAS_PIPEWIRE)
    list(APPEND INSIGHTOS_SOURCES
        src/discovery/pipewire_discovery.cpp
        src/workers/pipewire_worker.cpp
    )
endif()

# ── IPC support library (ring buffer writer + unix socket) ───────────────
# Shared by insightosd (data delivery) and any external consumers/examples.

add_library(insightos_ipc_support STATIC
    src/ipc/ipc.cpp
    src/ipc/unix_socket.cpp
)

target_include_directories(insightos_ipc_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(insightos_ipc_support PUBLIC cxx_std_20)
target_compile_options(insightos_ipc_support PRIVATE -Wall -Wextra -Wpedantic)

add_library(insightos_request_support STATIC
    src/uri.cpp
    src/request_support.cpp
)

target_include_directories(insightos_request_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(insightos_request_support PUBLIC cxx_std_20)
target_compile_options(insightos_request_support PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(insightos_request_support
    PUBLIC
        nlohmann_json::nlohmann_json
)

# ── Executable ───────────────────────────────────────────────────────────

add_executable(insightosd ${INSIGHTOS_SOURCES})

target_include_directories(insightosd
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        $<$<BOOL:${INSIGHTOS_HAS_ORBBEC}>:${INSIGHTOS_ORBBEC_SDK_INCLUDE}>
        $<$<BOOL:${INSIGHTOS_HAS_PIPEWIRE}>:${PIPEWIRE_INCLUDE_DIRS}>
)

target_compile_features(insightosd PRIVATE cxx_std_20)
target_compile_options(insightosd PRIVATE -Wall -Wextra -Wpedantic)

target_compile_definitions(insightosd PRIVATE
    $<$<BOOL:${INSIGHTOS_HAS_ORBBEC}>:INSIGHTOS_HAS_ORBBEC>
    $<$<BOOL:${INSIGHTOS_HAS_PIPEWIRE}>:INSIGHTOS_HAS_PIPEWIRE>
)

# ── Link libraries ──────────────────────────────────────────────────────

target_link_libraries(insightosd PRIVATE
    httplib::httplib
    insightos_ipc_support
    insightos_request_support
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
    Threads::Threads
)

if (INSIGHTOS_HAS_ORBBEC)
    target_link_libraries(insightosd PRIVATE
        ${ORBBEC_SDK_LIB}
        ${ORBBEC_USB_LIB}
        ${ORBBEC_DEPTH_ENGINE_LIB}
        ${ORBBEC_LIVE555_LIB}
    )
endif()
if (INSIGHTOS_HAS_PIPEWIRE)
    target_link_libraries(insightosd PRIVATE ${PIPEWIRE_LIBRARIES})
endif()

# ── Tests ────────────────────────────────────────────────────────────────

enable_testing()

add_executable(device_store_test
    tests/device_store_test.cpp
    src/device_store.cpp
    src/types.cpp
)

target_include_directories(device_store_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(device_store_test PRIVATE cxx_std_20)
target_compile_options(device_store_test PRIVATE -Wall -Wextra -Wpedantic)

target_link_libraries(device_store_test PRIVATE
    SQLite::SQLite3
)

add_test(NAME device_store_test COMMAND device_store_test)

add_executable(catalog_test
    tests/catalog_test.cpp
    src/catalog.cpp
    src/types.cpp
)

target_include_directories(catalog_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(catalog_test PRIVATE cxx_std_20)
target_compile_options(catalog_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME catalog_test COMMAND catalog_test)

set(REST_SERVER_TEST_SOURCES
    tests/rest_server_test.cpp
    src/types.cpp
    src/catalog.cpp
    src/device_store.cpp
    src/session_manager.cpp
    src/api/rest_server.cpp
    src/ipc/channel_registry.cpp
    src/workers/synthetic_worker.cpp
    src/workers/v4l2_worker.cpp
)

if (INSIGHTOS_HAS_ORBBEC)
    list(APPEND REST_SERVER_TEST_SOURCES src/workers/orbbec_worker.cpp)
endif()
if (INSIGHTOS_HAS_PIPEWIRE)
    list(APPEND REST_SERVER_TEST_SOURCES src/workers/pipewire_worker.cpp)
endif()

add_executable(rest_server_test ${REST_SERVER_TEST_SOURCES})

target_include_directories(rest_server_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    $<$<BOOL:${INSIGHTOS_HAS_ORBBEC}>:${INSIGHTOS_ORBBEC_SDK_INCLUDE}>
    $<$<BOOL:${INSIGHTOS_HAS_PIPEWIRE}>:${PIPEWIRE_INCLUDE_DIRS}>
)

target_compile_features(rest_server_test PRIVATE cxx_std_20)
target_compile_options(rest_server_test PRIVATE -Wall -Wextra -Wpedantic)

target_compile_definitions(rest_server_test PRIVATE
    $<$<BOOL:${INSIGHTOS_HAS_ORBBEC}>:INSIGHTOS_HAS_ORBBEC>
    $<$<BOOL:${INSIGHTOS_HAS_PIPEWIRE}>:INSIGHTOS_HAS_PIPEWIRE>
)

target_link_libraries(rest_server_test PRIVATE
    httplib::httplib
    insightos_ipc_support
    insightos_request_support
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
    Threads::Threads
)

if (INSIGHTOS_HAS_ORBBEC)
    target_link_libraries(rest_server_test PRIVATE
        ${ORBBEC_SDK_LIB}
        ${ORBBEC_USB_LIB}
        ${ORBBEC_DEPTH_ENGINE_LIB}
        ${ORBBEC_LIVE555_LIB}
    )
endif()
if (INSIGHTOS_HAS_PIPEWIRE)
    target_link_libraries(rest_server_test PRIVATE ${PIPEWIRE_LIBRARIES})
endif()

add_test(NAME rest_server_test COMMAND rest_server_test)

add_executable(request_support_test
    tests/request_support_test.cpp
)

target_include_directories(request_support_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(request_support_test PRIVATE cxx_std_20)
target_compile_options(request_support_test PRIVATE -Wall -Wextra -Wpedantic)

target_link_libraries(request_support_test PRIVATE
    insightos_request_support
)

add_test(NAME request_support_test COMMAND request_support_test)
