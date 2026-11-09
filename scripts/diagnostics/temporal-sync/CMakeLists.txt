cmake_minimum_required(VERSION 3.24)
project(ChronoxTemporalDiagnostic LANGUAGES C CXX)
include(CTest)
get_filename_component(CHRONOX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(SQLITE_SOURCE_DIR "${CHRONOX_ROOT}/build/_deps/sqlite3-src" CACHE PATH "Existing SQLite amalgamation; no download")
if(NOT EXISTS "${SQLITE_SOURCE_DIR}/sqlite3.c")
    message(FATAL_ERROR "Pass -DSQLITE_SOURCE_DIR=<existing amalgamation directory>")
endif()
add_library(diagnostic_sqlite STATIC "${SQLITE_SOURCE_DIR}/sqlite3.c")
target_include_directories(diagnostic_sqlite PUBLIC "${SQLITE_SOURCE_DIR}")
target_compile_definitions(diagnostic_sqlite PRIVATE SQLITE_DQS=0 SQLITE_THREADSAFE=1)
add_executable(temporal_sync_tests
    temporal_sync_tests.cpp
    "${CHRONOX_ROOT}/src/core/calendar_period.cpp"
    "${CHRONOX_ROOT}/src/core/session_tracker.cpp"
    "${CHRONOX_ROOT}/src/core/temporal_engine.cpp"
    "${CHRONOX_ROOT}/src/core/utc_timestamp_clock.cpp"
    "${CHRONOX_ROOT}/src/storage/session_database.cpp")
target_include_directories(temporal_sync_tests PRIVATE "${CHRONOX_ROOT}/src/core" "${CHRONOX_ROOT}/src/storage")
target_link_libraries(temporal_sync_tests PRIVATE diagnostic_sqlite)
target_compile_features(temporal_sync_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(temporal_sync_tests PRIVATE /W4 /permissive-)
    target_compile_options(diagnostic_sqlite PRIVATE /W0)
endif()
foreach(scenario baseline forward backward midnight midnight-offset sleep-qpc-advances sleep-qpc-pauses
        resume-forward resume-backward restart restart-future duplicates)
    add_test(NAME temporal_${scenario} COMMAND temporal_sync_tests ${scenario})
endforeach()
