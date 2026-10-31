add_library(chronox_core
   core/calendar_period.cpp
   core/session_tracker.cpp
   core/utc_timestamp_clock.cpp
)
target_include_directories(chronox_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core
)
target_compile_features(chronox_core PUBLIC cxx_std_20)

add_library(chronox_sqlite STATIC
    ${sqlite3_SOURCE_DIR}/sqlite3.c
)
target_include_directories(chronox_sqlite PUBLIC ${sqlite3_SOURCE_DIR})
target_compile_definitions(chronox_sqlite PRIVATE
    SQLITE_DQS=0
    SQLITE_THREADSAFE=1
)
if(MSVC)
    target_compile_options(chronox_sqlite PRIVATE /W0)
else()
    target_compile_options(chronox_sqlite PRIVATE -w)
endif()

add_library(chronox_storage
    storage/session_database.cpp
)
target_include_directories(chronox_storage PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/storage
)
target_link_libraries(chronox_storage PUBLIC chronox_core PRIVATE chronox_sqlite)
target_compile_features(chronox_storage PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(chronox_core PRIVATE /W4 /permissive-)
    target_compile_options(chronox_storage PRIVATE /W4 /permissive-)
else()
    target_compile_options(chronox_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(chronox_storage PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_executable(chronox
        windows/main.cpp
        windows/windows_session_monitor.cpp
    )
    target_link_libraries(chronox PRIVATE chronox_storage Wtsapi32 User32)
    target_compile_definitions(chronox PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    if(MSVC)
        target_compile_options(chronox PRIVATE /W4 /permissive-)
    endif()
endif()

if(BUILD_TESTING)
    add_executable(chronox_core_tests
        tests/session_tracker_tests.cpp
    )
    target_link_libraries(chronox_core_tests PRIVATE chronox_core)
    if(MSVC)
        target_compile_options(chronox_core_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(chronox_core_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME chronox_core_tests COMMAND chronox_core_tests)

    add_executable(chronox_storage_tests
        tests/session_database_tests.cpp
    )
    target_link_libraries(chronox_storage_tests PRIVATE chronox_storage chronox_sqlite)
    if(MSVC)
        target_compile_options(chronox_storage_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(chronox_storage_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME chronox_storage_tests COMMAND chronox_storage_tests)

    if(WIN32)
        add_executable(chronox_windows_session_monitor_tests
            tests/windows_session_monitor_tests.cpp
            windows/windows_session_monitor.cpp
        )
        target_include_directories(chronox_windows_session_monitor_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/windows
        )
        target_link_libraries(chronox_windows_session_monitor_tests PRIVATE
            chronox_core Wtsapi32 User32
        )
        target_compile_features(chronox_windows_session_monitor_tests PRIVATE cxx_std_20)
        target_compile_definitions(chronox_windows_session_monitor_tests PRIVATE
            UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00
        )
        if(MSVC)
            target_compile_options(chronox_windows_session_monitor_tests PRIVATE
                /W4 /permissive-
            )
        endif()
        add_test(NAME chronox_windows_session_monitor_tests
                 COMMAND chronox_windows_session_monitor_tests)
    endif()
endif()
