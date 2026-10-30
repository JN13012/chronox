add_library(chronox_core
   core/session_tracker.cpp
)
target_include_directories(chronox_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core
)
target_compile_features(chronox_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(chronox_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(chronox_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_executable(chronox
        windows/main.cpp
        windows/windows_session_monitor.cpp
    )
    target_link_libraries(chronox PRIVATE chronox_core Wtsapi32 User32)
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
endif()
