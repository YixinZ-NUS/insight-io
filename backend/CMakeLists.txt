# role: bootstrap the standalone insight-io backend build.
# revision: 2026-03-25 bootstrap-runtime-build
# major changes: reintroduce explicit schema bootstrap, health server, and
# focused tests. See docs/past-tasks.md for the verification record.

include(FetchContent)

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

add_library(insightio_backend_support STATIC
    src/schema_store.cpp
    src/api/rest_server.cpp
)

target_include_directories(insightio_backend_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(insightio_backend_support PUBLIC cxx_std_20)
target_compile_options(insightio_backend_support PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(insightio_backend_support
    PRIVATE
        INSIGHTIO_SCHEMA_PATH="${INSIGHTIO_SCHEMA_PATH}"
)

target_link_libraries(insightio_backend_support
    PUBLIC
        SQLite::SQLite3
        Threads::Threads
        httplib::httplib
        nlohmann_json::nlohmann_json
)

add_executable(insightiod
    src/main.cpp
)

target_include_directories(insightiod PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(insightiod PRIVATE cxx_std_20)
target_compile_options(insightiod PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(insightiod PRIVATE insightio_backend_support)

add_executable(schema_store_test
    tests/schema_store_test.cpp
)

target_include_directories(schema_store_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(schema_store_test PRIVATE cxx_std_20)
target_compile_options(schema_store_test PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(schema_store_test PRIVATE insightio_backend_support SQLite::SQLite3)

add_executable(rest_server_test
    tests/rest_server_test.cpp
)

target_include_directories(rest_server_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(rest_server_test PRIVATE cxx_std_20)
target_compile_options(rest_server_test PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(rest_server_test PRIVATE insightio_backend_support)

add_test(NAME schema_store_test COMMAND schema_store_test)
add_test(NAME rest_server_test COMMAND rest_server_test)
