cmake_minimum_required(VERSION 3.24)
project(ChronoxDeploymentProbe LANGUAGES C CXX)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_library(probe_sqlite STATIC "${ROOT}/build/_deps/sqlite3-src/sqlite3.c")
target_include_directories(probe_sqlite PUBLIC "${ROOT}/build/_deps/sqlite3-src")
target_compile_definitions(probe_sqlite PRIVATE SQLITE_DQS=0 SQLITE_THREADSAFE=1)
add_executable(chronox_deployment_probe deployment_probe.cpp
    "${ROOT}/src/core/calendar_period.cpp"
    "${ROOT}/src/core/session_tracker.cpp"
    "${ROOT}/src/core/temporal_engine.cpp"
    "${ROOT}/src/windows/dashboard_data.cpp"
    "${ROOT}/src/storage/session_database.cpp")
target_include_directories(chronox_deployment_probe PRIVATE
    "${ROOT}/src/core" "${ROOT}/src/storage" "${ROOT}/src/windows")
target_compile_features(chronox_deployment_probe PRIVATE cxx_std_20)
target_link_libraries(chronox_deployment_probe PRIVATE probe_sqlite)
