# All sources are UTF-8. MSVC otherwise decodes BOM-less UI literals with the
# machine code page, turning accented text and punctuation into mojibake.
if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>")
endif()

add_library(chronox_core
   core/calendar_period.cpp
   core/session_tracker.cpp
   core/utc_timestamp_clock.cpp
   core/temporal_engine.cpp
   core/activity_tracker.cpp
   core/temporal_diagnostics.cpp
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
    add_executable(chronox WIN32
        windows/chronox.rc
        windows/chronox_widget.cpp
        windows/dashboard_data.cpp
        windows/dashboard_window.cpp
        windows/main.cpp
        windows/windows_time_source.cpp
        windows/windows_input_source.cpp
        windows/widget_presentation.cpp
        windows/widget_preferences.cpp
        windows/activity_preferences.cpp
        windows/window_coordinator.cpp
        windows/windows_session_monitor.cpp
        windows/windows_startup.cpp
    )
    target_include_directories(chronox PRIVATE
        "${PROJECT_SOURCE_DIR}"
    )
    target_link_libraries(chronox PRIVATE
        chronox_storage
        Wtsapi32
        User32
        Shell32
        Advapi32
        Ole32
        Windowscodecs
        D2d1
        Dwrite
        Dwmapi
        Comctl32
        Mincore
    )
    target_compile_definitions(chronox PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
    )
    if(MSVC)
        target_compile_options(chronox PRIVATE /W4 /permissive-)
    endif()
    add_custom_command(TARGET chronox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/assets/ui"
            "$<TARGET_FILE_DIR:chronox>/assets/ui"
        VERBATIM
    )
endif()

if(BUILD_TESTING)
    add_executable(chronox_ui_layout_tests tests/ui_layout_tests.cpp)
    target_compile_features(chronox_ui_layout_tests PRIVATE cxx_std_20)
    add_test(NAME chronox_ui_layout_tests COMMAND chronox_ui_layout_tests)
    if(WIN32)
        target_link_libraries(chronox_ui_layout_tests PRIVATE Dwrite Ole32)
        target_compile_definitions(chronox_ui_layout_tests PRIVATE NOMINMAX)
    endif()
    add_executable(chronox_activity_migration_probe EXCLUDE_FROM_ALL tests/activity_migration_probe.cpp)
    target_link_libraries(chronox_activity_migration_probe PRIVATE chronox_storage)
    add_executable(chronox_activity_storage_tests tests/activity_storage_tests.cpp windows/dashboard_data.cpp)
    target_link_libraries(chronox_activity_storage_tests PRIVATE chronox_storage chronox_sqlite)
    add_test(NAME chronox_activity_storage_tests COMMAND chronox_activity_storage_tests)
    add_executable(chronox_activity_tracker_tests tests/activity_tracker_tests.cpp)
    target_link_libraries(chronox_activity_tracker_tests PRIVATE chronox_core)
    add_test(NAME chronox_activity_tracker_tests COMMAND chronox_activity_tracker_tests)
    add_executable(chronox_temporal_diagnostics_tests tests/temporal_diagnostics_tests.cpp)
    target_link_libraries(chronox_temporal_diagnostics_tests PRIVATE chronox_core)
    add_test(NAME chronox_temporal_diagnostics_tests COMMAND chronox_temporal_diagnostics_tests)
    add_executable(chronox_temporal_scenarios ../scripts/diagnostics/temporal-sync/temporal_sync_tests.cpp)
    target_link_libraries(chronox_temporal_scenarios PRIVATE chronox_storage)
    foreach(scenario baseline forward backward midnight midnight-offset sleep-qpc-advances sleep-qpc-pauses resume-forward resume-backward restart restart-future duplicates)
        add_test(NAME temporal_${scenario} COMMAND chronox_temporal_scenarios ${scenario})
    endforeach()
    add_executable(chronox_temporal_storage_tests tests/temporal_storage_tests.cpp)
    target_link_libraries(chronox_temporal_storage_tests PRIVATE chronox_storage chronox_sqlite)
    add_test(NAME chronox_temporal_storage_tests COMMAND chronox_temporal_storage_tests)
    add_executable(chronox_temporal_engine_tests tests/temporal_engine_tests.cpp)
    target_link_libraries(chronox_temporal_engine_tests PRIVATE chronox_core)
    add_test(NAME chronox_temporal_engine_tests COMMAND chronox_temporal_engine_tests)
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

    add_executable(chronox_dashboard_data_tests
        tests/dashboard_data_tests.cpp
        windows/dashboard_data.cpp
    )
    target_link_libraries(chronox_dashboard_data_tests PRIVATE chronox_storage)
    target_compile_features(chronox_dashboard_data_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(chronox_dashboard_data_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(chronox_dashboard_data_tests PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(NAME chronox_dashboard_data_tests COMMAND chronox_dashboard_data_tests)

    add_executable(chronox_widget_tests
        tests/widget_presentation_tests.cpp
        windows/widget_presentation.cpp
    )
    target_compile_features(chronox_widget_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(chronox_widget_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(chronox_widget_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME chronox_widget_tests COMMAND chronox_widget_tests)

    if(WIN32)
        add_executable(chronox_tray_activation_tests tests/tray_activation_tests.cpp windows/chronox_widget.cpp windows/widget_presentation.cpp windows/chronox.rc)
        target_include_directories(chronox_tray_activation_tests PRIVATE "${PROJECT_SOURCE_DIR}")
        set_target_properties(chronox_tray_activation_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tray-tests")
        target_link_libraries(chronox_tray_activation_tests PRIVATE chronox_core User32 Shell32 Ole32 Windowscodecs D2d1 Dwrite)
        target_compile_definitions(chronox_tray_activation_tests PRIVATE UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
        add_custom_command(TARGET chronox_tray_activation_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets/ui" "$<TARGET_FILE_DIR:chronox_tray_activation_tests>/assets/ui")
        add_test(NAME chronox_tray_activation_tests COMMAND chronox_tray_activation_tests)
        set_tests_properties(chronox_tray_activation_tests PROPERTIES TIMEOUT 20)
        add_executable(chronox_activity_dashboard_tests tests/activity_dashboard_tests.cpp windows/dashboard_window.cpp windows/dashboard_data.cpp windows/chronox.rc)
        target_include_directories(chronox_activity_dashboard_tests PRIVATE "${PROJECT_SOURCE_DIR}")
        target_link_libraries(chronox_activity_dashboard_tests PRIVATE chronox_storage User32 D2d1 Dwrite Dwmapi Comctl32 Ole32)
        target_compile_definitions(chronox_activity_dashboard_tests PRIVATE UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
        add_test(NAME chronox_activity_dashboard_tests COMMAND chronox_activity_dashboard_tests)
        set_tests_properties(chronox_activity_dashboard_tests PROPERTIES TIMEOUT 20)
        add_executable(chronox_dashboard_review EXCLUDE_FROM_ALL tests/dashboard_review.cpp windows/dashboard_window.cpp windows/dashboard_data.cpp windows/chronox.rc)
        target_include_directories(chronox_dashboard_review PRIVATE "${PROJECT_SOURCE_DIR}")
        target_link_libraries(chronox_dashboard_review PRIVATE chronox_storage User32 D2d1 Dwrite Dwmapi Comctl32 Ole32 Psapi)
        target_compile_definitions(chronox_dashboard_review PRIVATE UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
        add_executable(chronox_titanium_style_tests tests/titanium_style_tests.cpp)
        target_compile_features(chronox_titanium_style_tests PRIVATE cxx_std_20)
        target_link_libraries(chronox_titanium_style_tests PRIVATE Dwrite Ole32)
        target_compile_definitions(chronox_titanium_style_tests PRIVATE UNICODE _UNICODE NOMINMAX)
        add_test(NAME chronox_titanium_style_tests COMMAND chronox_titanium_style_tests)
        add_executable(chronox_titanium_interaction_tests tests/activity_dashboard_tests.cpp windows/dashboard_window.cpp windows/dashboard_data.cpp windows/chronox.rc)
        target_include_directories(chronox_titanium_interaction_tests PRIVATE "${PROJECT_SOURCE_DIR}")
        target_link_libraries(chronox_titanium_interaction_tests PRIVATE chronox_storage User32 D2d1 Dwrite Dwmapi Comctl32 Ole32)
        target_compile_definitions(chronox_titanium_interaction_tests PRIVATE CHRONOX_TITANIUM_PROTOTYPE UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
        add_test(NAME chronox_titanium_interaction_tests COMMAND chronox_titanium_interaction_tests)
        set_tests_properties(chronox_titanium_interaction_tests PROPERTIES TIMEOUT 20)
        # E2.2: opt-in presentation fixture; production and other pages stay E2.1.
        add_executable(chronox_dashboard_titanium_review EXCLUDE_FROM_ALL tests/dashboard_review.cpp windows/dashboard_window.cpp windows/dashboard_data.cpp windows/chronox.rc)
        target_include_directories(chronox_dashboard_titanium_review PRIVATE "${PROJECT_SOURCE_DIR}")
        target_link_libraries(chronox_dashboard_titanium_review PRIVATE chronox_storage User32 D2d1 Dwrite Dwmapi Comctl32 Ole32 Psapi)
        target_compile_definitions(chronox_dashboard_titanium_review PRIVATE CHRONOX_TITANIUM_PROTOTYPE UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
        if(MSVC)
            target_compile_options(chronox_dashboard_titanium_review PRIVATE /W4 /permissive-)
        endif()
        add_executable(chronox_activity_resource_probe EXCLUDE_FROM_ALL tests/activity_resource_probe.cpp windows/windows_time_source.cpp windows/windows_input_source.cpp)
        target_link_libraries(chronox_activity_resource_probe PRIVATE chronox_storage User32 Psapi Mincore)
        target_compile_definitions(chronox_activity_resource_probe PRIVATE UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
        add_executable(chronox_activity_preferences_tests tests/activity_preferences_tests.cpp windows/activity_preferences.cpp)
        target_link_libraries(chronox_activity_preferences_tests PRIVATE chronox_core Advapi32)
        target_compile_definitions(chronox_activity_preferences_tests PRIVATE UNICODE _UNICODE NOMINMAX)
        add_test(NAME chronox_activity_preferences_tests COMMAND chronox_activity_preferences_tests)
        add_executable(chronox_dashboard_smoke WIN32 EXCLUDE_FROM_ALL
            windows/chronox.rc
            tests/dashboard_window_smoke.cpp
            windows/dashboard_data.cpp
            windows/dashboard_window.cpp
            windows/window_coordinator.cpp
        )
        target_include_directories(chronox_dashboard_smoke PRIVATE
            "${PROJECT_SOURCE_DIR}"
        )
        target_link_libraries(chronox_dashboard_smoke PRIVATE
            chronox_storage
            User32
            D2d1
            Dwrite
            Dwmapi
            Comctl32
        )
        target_compile_features(chronox_dashboard_smoke PRIVATE cxx_std_20)
        target_compile_definitions(chronox_dashboard_smoke PRIVATE
            UNICODE
            _UNICODE
            NOMINMAX
            WINVER=0x0A00
            _WIN32_WINNT=0x0A00
        )
        if(MSVC)
            target_compile_options(chronox_dashboard_smoke PRIVATE
                /W4 /permissive-
            )
        endif()

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

        add_executable(chronox_windows_startup_tests
            tests/windows_startup_tests.cpp
            windows/windows_startup.cpp
        )
        target_include_directories(chronox_windows_startup_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/windows
        )
        target_compile_features(chronox_windows_startup_tests PRIVATE cxx_std_20)
        target_compile_definitions(chronox_windows_startup_tests PRIVATE
            UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00
        )
        target_link_libraries(chronox_windows_startup_tests PRIVATE
            Advapi32 Ole32
        )
        if(MSVC)
            target_compile_options(chronox_windows_startup_tests PRIVATE
                /W4 /permissive-
            )
        endif()
        add_test(NAME chronox_windows_startup_tests
                 COMMAND chronox_windows_startup_tests)

        add_executable(chronox_widget_preferences_tests
            tests/widget_preferences_tests.cpp
            windows/widget_preferences.cpp
        )
        target_include_directories(chronox_widget_preferences_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/windows
        )
        target_compile_features(chronox_widget_preferences_tests PRIVATE cxx_std_20)
        target_compile_definitions(chronox_widget_preferences_tests PRIVATE
            UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00
        )
        target_link_libraries(chronox_widget_preferences_tests PRIVATE Advapi32 Ole32)
        if(MSVC)
            target_compile_options(chronox_widget_preferences_tests PRIVATE
                /W4 /permissive-
            )
        endif()
        add_test(NAME chronox_widget_preferences_tests
                 COMMAND chronox_widget_preferences_tests)
    endif()
endif()
