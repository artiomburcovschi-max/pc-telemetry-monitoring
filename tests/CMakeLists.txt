add_executable(orion_core_tests core_tests.cpp)
target_link_libraries(orion_core_tests PRIVATE ORION::Core)
add_test(NAME orion_core_tests COMMAND orion_core_tests)

add_executable(orion_observation_clock_tests observation_clock_tests.cpp)
target_link_libraries(orion_observation_clock_tests PRIVATE ORION::Core)
add_test(NAME orion_observation_clock_tests COMMAND orion_observation_clock_tests)

add_executable(orion_network_counters_tests network_counters_tests.cpp)
target_link_libraries(orion_network_counters_tests PRIVATE ORION::Core)
add_test(NAME orion_network_counters_tests COMMAND orion_network_counters_tests)

add_executable(orion_platform_smoke platform_smoke.cpp)
target_link_libraries(orion_platform_smoke PRIVATE ORION::Platform)
add_test(NAME orion_platform_smoke COMMAND orion_platform_smoke)
set_tests_properties(orion_platform_smoke PROPERTIES TIMEOUT 10)

if(TARGET orion_diagnostics)
    add_executable(orion_diagnostic_schema_tests diagnostic_schema_tests.cpp)
    target_link_libraries(orion_diagnostic_schema_tests PRIVATE ORION::Diagnostics)
    add_test(NAME orion_diagnostic_schema_tests COMMAND orion_diagnostic_schema_tests)

    add_executable(orion_contract_tests contract_tests.cpp)
    target_link_libraries(orion_contract_tests PRIVATE ORION::Diagnostics)
    add_test(NAME orion_contract_tests COMMAND orion_contract_tests)

    add_executable(orion_incident_findings_tests incident_findings_tests.cpp)
    target_link_libraries(orion_incident_findings_tests PRIVATE ORION::Diagnostics)
    add_test(NAME orion_incident_findings_tests COMMAND orion_incident_findings_tests)

    add_executable(orion_system_diagnostics_tests system_diagnostics_collector_tests.cpp)
    target_link_libraries(orion_system_diagnostics_tests PRIVATE ORION::Diagnostics)
    add_test(NAME orion_system_diagnostics_tests COMMAND orion_system_diagnostics_tests)
    set_tests_properties(orion_system_diagnostics_tests PROPERTIES TIMEOUT 30)
    add_executable(orion_system_log_limits_tests system_log_limits_tests.cpp)
    target_link_libraries(orion_system_log_limits_tests PRIVATE ORION::Diagnostics)
    add_test(NAME orion_system_log_limits_tests COMMAND orion_system_log_limits_tests)
    set_tests_properties(orion_system_log_limits_tests PROPERTIES TIMEOUT 15)

    add_executable(orion_runtime_diagnostics_tests runtime_diagnostics_tests.cpp)
    target_link_libraries(orion_runtime_diagnostics_tests PRIVATE ORION::Diagnostics)
    add_test(NAME orion_runtime_diagnostics_tests COMMAND orion_runtime_diagnostics_tests)
    add_executable(orion_app_monitor_data_tests app_monitor_data_tests.cpp)
    target_link_libraries(orion_app_monitor_data_tests PRIVATE ORION::Diagnostics)
    add_test(NAME orion_app_monitor_data_tests COMMAND orion_app_monitor_data_tests)
    add_executable(orion_incident_data_tests incident_data_tests.cpp)
    target_link_libraries(orion_incident_data_tests PRIVATE ORION::Diagnostics)
    add_test(NAME orion_incident_data_tests COMMAND orion_incident_data_tests)

    add_test(
        NAME orion_golden_contract
        COMMAND orion_golden_contract "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/diagnostic_golden_cases.json"
    )

    if(WIN32)
        get_filename_component(_orion_compiler_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(
            orion_diagnostic_schema_tests
            orion_contract_tests
            orion_incident_findings_tests
            orion_system_diagnostics_tests
            orion_system_log_limits_tests
            orion_runtime_diagnostics_tests
            orion_app_monitor_data_tests
            orion_incident_data_tests
            orion_golden_contract
            PROPERTIES ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_compiler_runtime_dir}"
        )
    endif()
endif()

if(TARGET orion_storage)
    add_executable(orion_telemetry_files_tests telemetry_files_tests.cpp)
    target_link_libraries(orion_telemetry_files_tests PRIVATE ORION::Storage)
    add_test(NAME orion_telemetry_files_tests COMMAND orion_telemetry_files_tests)
    set_tests_properties(orion_telemetry_files_tests PROPERTIES TIMEOUT 10)
    add_executable(orion_storage_tests storage_tests.cpp)
    target_link_libraries(orion_storage_tests PRIVATE ORION::Storage)
    add_test(NAME orion_storage_tests COMMAND orion_storage_tests)
    if(WIN32)
        get_filename_component(_orion_storage_compiler_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_storage_tests orion_telemetry_files_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_storage_compiler_runtime_dir}"
        )
    endif()
endif()

if(TARGET orion)
    qt_add_executable(orion_diagnostic_layout_tests diagnostic_layout_tests.cpp
        ../ui/diagnostic_dock_layout.cpp ../ui/diagnostic_dock_layout.h
        ../ui/widgets/independent_dock_widget.cpp ../ui/widgets/independent_dock_widget.h)
    target_include_directories(orion_diagnostic_layout_tests PRIVATE ../ui)
    target_link_libraries(orion_diagnostic_layout_tests PRIVATE Qt6::Widgets)
    add_test(NAME orion_diagnostic_layout_tests COMMAND orion_diagnostic_layout_tests)
    set_tests_properties(orion_diagnostic_layout_tests PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        get_filename_component(_layout_runtime "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_diagnostic_layout_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_layout_runtime}")
    endif()

    qt_add_executable(orion_independent_dock_tests independent_dock_tests.cpp
        ../ui/widgets/independent_dock_widget.cpp ../ui/widgets/independent_dock_widget.h)
    target_include_directories(orion_independent_dock_tests PRIVATE ../ui)
    target_link_libraries(orion_independent_dock_tests PRIVATE Qt6::Widgets)
    add_test(NAME orion_independent_dock_tests COMMAND orion_independent_dock_tests)
    set_tests_properties(orion_independent_dock_tests PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        target_link_libraries(orion_independent_dock_tests PRIVATE user32)
        # The regression is native HWND ownership; offscreen cannot verify it.
        set_tests_properties(orion_independent_dock_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
        get_filename_component(_dock_runtime "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_independent_dock_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_dock_runtime}")
    endif()

    qt_add_executable(orion_incident_tests incident_tests.cpp
        ../ui/incident_worker.cpp ../ui/incident_worker.h)
    set_target_properties(orion_incident_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_incident_tests PRIVATE ../ui)
    target_link_libraries(orion_incident_tests PRIVATE ORION::Diagnostics Qt6::Core)
    add_test(NAME orion_incident_tests COMMAND orion_incident_tests)
    set_tests_properties(orion_incident_tests PROPERTIES TIMEOUT 15)
    if(WIN32)
        get_filename_component(_incident_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_incident_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_incident_runtime_dir}")
    endif()

    qt_add_executable(orion_full_scan_tests
        full_scan_tests.cpp
        ../ui/full_scan_worker.cpp ../ui/full_scan_worker.h
        ../ui/full_scan_dialog.cpp ../ui/full_scan_dialog.h
        ../ui/deep_scan_worker.cpp ../ui/deep_scan_worker.h
        ../ui/scan_report_sections.cpp ../ui/scan_report_sections.h
        ../ui/hardware_inventory_worker.cpp ../ui/hardware_inventory_worker.h
        ../ui/hardware_inventory_details.cpp ../ui/hardware_inventory_details.h
        ../ui/internet_tools_worker.cpp ../ui/internet_tools_worker.h
        ../ui/stress_worker.cpp ../ui/stress_worker.h)
    set_target_properties(orion_full_scan_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_full_scan_tests PRIVATE ../ui)
    target_link_libraries(orion_full_scan_tests PRIVATE ORION::Platform ORION::Diagnostics
        Qt6::Core Qt6::Gui Qt6::Network Qt6::Widgets)
    if(WIN32)
        target_link_libraries(orion_full_scan_tests PRIVATE
            d3d11 d3dcompiler dxgi iphlpapi ws2_32 wbemuuid ole32 oleaut32)
    endif()
    add_test(NAME orion_full_scan_tests COMMAND orion_full_scan_tests)
    set_tests_properties(orion_full_scan_tests PROPERTIES
        TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        get_filename_component(_orion_full_scan_runtime "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_full_scan_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_full_scan_runtime}")
    endif()

    qt_add_executable(orion_deep_scan_tests
        deep_scan_tests.cpp
        ../ui/deep_scan_worker.cpp ../ui/deep_scan_worker.h
        ../ui/scan_report_sections.cpp ../ui/scan_report_sections.h
        ../ui/deep_scan_dialog.cpp ../ui/deep_scan_dialog.h
        ../ui/hardware_inventory_worker.cpp ../ui/hardware_inventory_worker.h
        ../ui/hardware_inventory_details.cpp ../ui/hardware_inventory_details.h
        ../ui/stress_worker.cpp ../ui/stress_worker.h)
    set_target_properties(orion_deep_scan_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_deep_scan_tests PRIVATE ../ui)
    target_link_libraries(orion_deep_scan_tests PRIVATE ORION::Platform ORION::Diagnostics Qt6::Widgets Qt6::Network)
    if(WIN32)
        target_link_libraries(orion_deep_scan_tests PRIVATE d3d11 d3dcompiler dxgi iphlpapi ws2_32 wbemuuid ole32 oleaut32)
    endif()
    add_test(NAME orion_deep_scan_tests COMMAND orion_deep_scan_tests)
    set_tests_properties(orion_deep_scan_tests PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        get_filename_component(_orion_deep_scan_runtime "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_deep_scan_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_deep_scan_runtime}")
    endif()
    qt_add_executable(orion_hardware_inventory_tests
        hardware_inventory_tests.cpp
        ../ui/hardware_inventory_worker.cpp
        ../ui/hardware_inventory_worker.h
        ../ui/hardware_inventory_details.cpp
        ../ui/hardware_inventory_details.h
    )
    set_target_properties(orion_hardware_inventory_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_hardware_inventory_tests PRIVATE ../ui)
    target_link_libraries(orion_hardware_inventory_tests PRIVATE Qt6::Core)
    if(WIN32)
        target_link_libraries(orion_hardware_inventory_tests PRIVATE wbemuuid ole32 oleaut32 dxgi iphlpapi ws2_32)
    endif()
    add_test(NAME orion_hardware_inventory_tests COMMAND orion_hardware_inventory_tests)
    set_tests_properties(orion_hardware_inventory_tests PROPERTIES TIMEOUT 10)
    if(WIN32)
        get_filename_component(_orion_hardware_test_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_hardware_inventory_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_hardware_test_runtime_dir}")
    endif()

    add_executable(orion_app_monitor_fixture app_monitor_fixture.cpp)
    target_link_libraries(orion_app_monitor_fixture PRIVATE Qt6::Core)

    qt_add_executable(orion_app_monitor_worker_tests
        app_monitor_worker_tests.cpp
        ../ui/app_process_session.cpp ../ui/app_process_session.h
        ../ui/app_monitor_worker.cpp
        ../ui/app_monitor_worker.h
    )
    set_target_properties(orion_app_monitor_worker_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_app_monitor_worker_tests PRIVATE ../ui)
    target_link_libraries(orion_app_monitor_worker_tests
        PRIVATE ORION::Diagnostics ORION::Platform Qt6::Core)
    add_dependencies(orion_app_monitor_worker_tests orion_app_monitor_fixture)
    add_test(NAME orion_app_monitor_worker_tests COMMAND orion_app_monitor_worker_tests)
    set_tests_properties(orion_app_monitor_worker_tests PROPERTIES TIMEOUT 60)
    if(WIN32)
        get_filename_component(_orion_app_monitor_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_app_monitor_worker_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_app_monitor_runtime_dir}")
    endif()

    qt_add_executable(orion_stress_worker_tests
        stress_worker_tests.cpp
        ../ui/stress_worker.cpp
        ../ui/stress_worker.h
    )
    set_target_properties(orion_stress_worker_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_stress_worker_tests PRIVATE ../ui)
    target_link_libraries(orion_stress_worker_tests
        PRIVATE ORION::Diagnostics ORION::Platform Qt6::Core)
    if(WIN32)
        target_link_libraries(orion_stress_worker_tests PRIVATE d3d11 d3dcompiler dxgi)
    endif()
    add_test(NAME orion_stress_worker_tests COMMAND orion_stress_worker_tests)
    set_tests_properties(orion_stress_worker_tests PROPERTIES TIMEOUT 15)
    if(WIN32)
        get_filename_component(_orion_stress_worker_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_stress_worker_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_stress_worker_runtime_dir}")
    endif()

    qt_add_executable(orion_telemetry_worker_tests
        telemetry_worker_tests.cpp
        ../ui/telemetry_worker.cpp
        ../ui/telemetry_worker.h
    )
    set_target_properties(orion_telemetry_worker_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_telemetry_worker_tests PRIVATE ../ui)
    target_link_libraries(orion_telemetry_worker_tests PRIVATE ORION::Platform ORION::Diagnostics Qt6::Core)
    add_test(NAME orion_telemetry_worker_tests COMMAND orion_telemetry_worker_tests)
    set_tests_properties(orion_telemetry_worker_tests PROPERTIES TIMEOUT 10)
    if(WIN32)
        get_filename_component(_orion_worker_compiler_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_telemetry_worker_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_worker_compiler_runtime_dir}"
        )
    endif()

    qt_add_executable(orion_ping_worker_tests
        ping_worker_tests.cpp
        ../ui/ping_worker.cpp
        ../ui/ping_worker.h
    )
    set_target_properties(orion_ping_worker_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_ping_worker_tests PRIVATE ../ui)
    target_link_libraries(orion_ping_worker_tests PRIVATE Qt6::Core Qt6::Network)
    add_test(NAME orion_ping_worker_tests COMMAND orion_ping_worker_tests)
    set_tests_properties(orion_ping_worker_tests PROPERTIES TIMEOUT 10)
    if(WIN32)
        get_filename_component(_orion_ping_worker_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_ping_worker_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_ping_worker_runtime_dir}")
    endif()

    qt_add_executable(orion_network_scanner_worker_tests
        network_scanner_worker_tests.cpp
        ../ui/network_scanner_worker.cpp
        ../ui/network_scanner_worker.h
        ../ui/network_identity.cpp
        ../ui/network_identity.h
    )
    set_target_properties(orion_network_scanner_worker_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_network_scanner_worker_tests PRIVATE ../ui)
    target_link_libraries(orion_network_scanner_worker_tests PRIVATE Qt6::Core Qt6::Network)
    if(WIN32)
        target_link_libraries(orion_network_scanner_worker_tests PRIVATE iphlpapi ws2_32)
    endif()
    add_test(NAME orion_network_scanner_worker_tests COMMAND orion_network_scanner_worker_tests)
    set_tests_properties(orion_network_scanner_worker_tests PROPERTIES TIMEOUT 10)
    if(WIN32)
        get_filename_component(_orion_network_scanner_test_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_network_scanner_worker_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_network_scanner_test_runtime_dir}")
    endif()

    qt_add_executable(orion_network_identity_tests
        network_identity_tests.cpp
        ../ui/network_identity.cpp
        ../ui/network_identity.h
        ../ui/network_scanner_worker.h
    )
    target_include_directories(orion_network_identity_tests PRIVATE ../ui)
    target_link_libraries(orion_network_identity_tests PRIVATE Qt6::Core Qt6::Network)
    if(WIN32)
        target_link_libraries(orion_network_identity_tests PRIVATE iphlpapi ws2_32)
    endif()

    qt_add_executable(orion_internet_tools_worker_tests
        internet_tools_worker_tests.cpp
        ../ui/internet_tools_worker.cpp
        ../ui/internet_tools_worker.h
    )
    set_target_properties(orion_internet_tools_worker_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_internet_tools_worker_tests PRIVATE ../ui)
    target_link_libraries(orion_internet_tools_worker_tests PRIVATE Qt6::Core Qt6::Network)
    add_test(NAME orion_internet_tools_worker_tests COMMAND orion_internet_tools_worker_tests)
    set_tests_properties(orion_internet_tools_worker_tests PROPERTIES TIMEOUT 10)
    if(WIN32)
        set_tests_properties(orion_internet_tools_worker_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_network_scanner_test_runtime_dir}")
    endif()
    add_test(NAME orion_network_identity_tests COMMAND orion_network_identity_tests)
    set_tests_properties(orion_network_identity_tests PROPERTIES TIMEOUT 10)
    if(WIN32)
        set_tests_properties(orion_network_identity_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_network_scanner_test_runtime_dir}")
    endif()

    qt_add_executable(orion_gamer_overlay_tests
        gamer_overlay_tests.cpp
        ../ui/overlay_fps_counter.cpp
        ../ui/overlay_fps_counter.h
        ../ui/ping_worker.cpp
        ../ui/ping_worker.h
        ../ui/widgets/gamer_overlay.cpp
        ../ui/widgets/gamer_overlay.h
    )
    set_target_properties(orion_gamer_overlay_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_gamer_overlay_tests PRIVATE ../ui)
    target_link_libraries(orion_gamer_overlay_tests
        PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Widgets)
    add_test(NAME orion_gamer_overlay_tests COMMAND orion_gamer_overlay_tests)
    set_tests_properties(orion_gamer_overlay_tests PROPERTIES
        TIMEOUT 10
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        get_filename_component(_orion_gamer_test_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_gamer_overlay_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_gamer_test_runtime_dir}")
    endif()

    qt_add_executable(orion_gauge_widget_tests
        gauge_widget_tests.cpp
        ../ui/widgets/gauge_widget.cpp
        ../ui/widgets/gauge_widget.h
    )
    set_target_properties(orion_gauge_widget_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_gauge_widget_tests PRIVATE ../ui)
    target_link_libraries(orion_gauge_widget_tests
        PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME orion_gauge_widget_tests COMMAND orion_gauge_widget_tests)
    set_tests_properties(orion_gauge_widget_tests PROPERTIES
        TIMEOUT 10
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        get_filename_component(_orion_gauge_test_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_gauge_widget_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_gauge_test_runtime_dir}")
    endif()

    qt_add_executable(orion_cpu_core_chart_widget_tests
        cpu_core_chart_widget_tests.cpp
        ../ui/widgets/cpu_core_chart_widget.cpp
        ../ui/widgets/cpu_core_chart_widget.h
    )
    set_target_properties(orion_cpu_core_chart_widget_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_cpu_core_chart_widget_tests PRIVATE ../ui)
    target_link_libraries(orion_cpu_core_chart_widget_tests
        PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME orion_cpu_core_chart_widget_tests COMMAND orion_cpu_core_chart_widget_tests)
    set_tests_properties(orion_cpu_core_chart_widget_tests PROPERTIES
        TIMEOUT 10
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(orion_cpu_core_chart_widget_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_gauge_test_runtime_dir}")
    endif()

    qt_add_executable(orion_details_panel_tests
        details_panel_tests.cpp
        ../ui/widgets/details_panel.cpp
        ../ui/widgets/details_panel.h
        ../ui/widgets/cpu_core_chart_widget.cpp
        ../ui/widgets/cpu_core_chart_widget.h
    )
    set_target_properties(orion_details_panel_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_details_panel_tests PRIVATE ../ui)
    target_link_libraries(orion_details_panel_tests PRIVATE ORION::Core Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME orion_details_panel_tests COMMAND orion_details_panel_tests)
    set_tests_properties(orion_details_panel_tests PROPERTIES TIMEOUT 10 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(orion_details_panel_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_gauge_test_runtime_dir}")
    endif()

    qt_add_executable(orion_network_traffic_chart_tests
        network_traffic_chart_tests.cpp
        ../ui/widgets/network_traffic_chart.cpp
        ../ui/widgets/network_traffic_chart.h
    )
    set_target_properties(orion_network_traffic_chart_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_network_traffic_chart_tests PRIVATE ../ui)
    target_link_libraries(orion_network_traffic_chart_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME orion_network_traffic_chart_tests COMMAND orion_network_traffic_chart_tests)
    set_tests_properties(orion_network_traffic_chart_tests PROPERTIES
        TIMEOUT 10 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(orion_network_traffic_chart_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_gauge_test_runtime_dir}")
    endif()

    qt_add_executable(orion_app_monitor_chart_tests
        app_monitor_chart_tests.cpp
        ../ui/widgets/app_monitor_chart.cpp
        ../ui/widgets/app_monitor_chart.h
    )
    set_target_properties(orion_app_monitor_chart_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_app_monitor_chart_tests PRIVATE ../ui)
    target_link_libraries(orion_app_monitor_chart_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME orion_app_monitor_chart_tests COMMAND orion_app_monitor_chart_tests)
    set_tests_properties(orion_app_monitor_chart_tests PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(orion_app_monitor_chart_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_gauge_test_runtime_dir}")
    endif()

    qt_add_executable(orion_startup_tests
        startup_tests.cpp
        ../ui/startup_sequence.cpp
        ../ui/startup_sequence.h
        ../ui/startup_dialog.cpp
        ../ui/startup_dialog.h
    )
    set_target_properties(orion_startup_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_startup_tests PRIVATE ../ui)
    target_link_libraries(orion_startup_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME orion_startup_tests COMMAND orion_startup_tests)
    set_tests_properties(orion_startup_tests PROPERTIES TIMEOUT 10 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(orion_startup_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_gauge_test_runtime_dir}")
    endif()

    qt_add_executable(orion_server_status_checker_tests
        server_status_checker_tests.cpp
        ../ui/server_status_checker.cpp
        ../ui/server_status_checker.h
    )
    set_target_properties(orion_server_status_checker_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_server_status_checker_tests PRIVATE ../ui)
    target_link_libraries(orion_server_status_checker_tests PRIVATE Qt6::Core Qt6::Network)
    add_test(NAME orion_server_status_checker_tests COMMAND orion_server_status_checker_tests)
    set_tests_properties(orion_server_status_checker_tests PROPERTIES TIMEOUT 10)
    if(WIN32)
        get_filename_component(_orion_server_checker_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_server_status_checker_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_server_checker_runtime_dir}")
    endif()

    qt_add_executable(orion_terminal_widget_tests
        terminal_widget_tests.cpp
        ../ui/terminal_text.cpp
        ../ui/terminal_text.h
        ../ui/widgets/terminal_widget.cpp
        ../ui/widgets/terminal_widget.h
    )
    set_target_properties(orion_terminal_widget_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_terminal_widget_tests PRIVATE ../ui)
    target_link_libraries(orion_terminal_widget_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME orion_terminal_widget_tests COMMAND orion_terminal_widget_tests)
    set_tests_properties(orion_terminal_widget_tests PROPERTIES
        TIMEOUT 10
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        get_filename_component(_orion_terminal_test_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_terminal_widget_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_terminal_test_runtime_dir}")
    endif()

    qt_add_executable(orion_process_worker_tests
        process_worker_tests.cpp
        ../ui/process_worker.cpp
        ../ui/process_worker.h
    )
    set_target_properties(orion_process_worker_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_process_worker_tests PRIVATE ../ui)
    target_link_libraries(orion_process_worker_tests PRIVATE ORION::Platform Qt6::Core)
    add_test(NAME orion_process_worker_tests COMMAND orion_process_worker_tests)
    set_tests_properties(orion_process_worker_tests PROPERTIES TIMEOUT 10)
    if(WIN32)
        get_filename_component(_orion_process_worker_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_process_worker_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_process_worker_runtime_dir}"
        )
    endif()

    qt_add_executable(orion_autostart_worker_tests
        autostart_worker_tests.cpp
        ../ui/autostart_worker.cpp
        ../ui/autostart_worker.h
    )
    set_target_properties(orion_autostart_worker_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_autostart_worker_tests PRIVATE ../ui)
    target_link_libraries(orion_autostart_worker_tests PRIVATE ORION::Platform Qt6::Core)
    add_test(NAME orion_autostart_worker_tests COMMAND orion_autostart_worker_tests)
    set_tests_properties(orion_autostart_worker_tests PROPERTIES TIMEOUT 10)
    if(WIN32)
        get_filename_component(_orion_autostart_worker_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_autostart_worker_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_autostart_worker_runtime_dir}")
    endif()

    qt_add_executable(orion_diagnostic_worker_tests
        diagnostic_worker_tests.cpp
        ../ui/diagnostic_worker.cpp
        ../ui/diagnostic_worker.h
    )
    set_target_properties(orion_diagnostic_worker_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_diagnostic_worker_tests PRIVATE ../ui)
    target_link_libraries(orion_diagnostic_worker_tests PRIVATE ORION::Diagnostics ORION::Platform Qt6::Core)
    add_test(NAME orion_diagnostic_worker_tests COMMAND orion_diagnostic_worker_tests)
    set_tests_properties(orion_diagnostic_worker_tests PROPERTIES TIMEOUT 10)
    if(WIN32)
        get_filename_component(_orion_diagnostic_worker_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_diagnostic_worker_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_diagnostic_worker_runtime_dir}")
    endif()

    qt_add_executable(orion_deep_telemetry_dialog_tests
        deep_telemetry_dialog_tests.cpp
        deep_telemetry_file_checks.cpp
        ../ui/deep_telemetry_dialog.cpp
        ../ui/deep_telemetry_dialog.h
    )
    set_target_properties(orion_deep_telemetry_dialog_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_deep_telemetry_dialog_tests PRIVATE ../ui)
    target_link_libraries(orion_deep_telemetry_dialog_tests
        PRIVATE ORION::Core ORION::Storage ORION::Diagnostics
            Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME orion_deep_telemetry_dialog_tests COMMAND orion_deep_telemetry_dialog_tests)
    set_tests_properties(orion_deep_telemetry_dialog_tests PROPERTIES
        TIMEOUT 10
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        get_filename_component(_orion_deep_test_compiler_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_deep_telemetry_dialog_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_deep_test_compiler_runtime_dir}")
    endif()

    qt_add_executable(orion_process_action_tests
        process_action_tests.cpp
        ../ui/process_action_worker.cpp ../ui/process_action_worker.h)
    set_target_properties(orion_process_action_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_process_action_tests PRIVATE ../ui)
    target_link_libraries(orion_process_action_tests PRIVATE ORION::Platform Qt6::Core)
    add_test(NAME orion_process_action_tests COMMAND orion_process_action_tests)
    set_tests_properties(orion_process_action_tests PROPERTIES TIMEOUT 20)
    if(WIN32)
        get_filename_component(_orion_action_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_process_action_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_action_runtime_dir}")
    endif()

    qt_add_executable(orion_ui_contract_tests
        ui_contract_tests.cpp
        diagnostic_layout_ui_checks.cpp
        table_parity_checks.cpp
        app_monitor_ui_checks.cpp
        floating_cards_ui_checks.cpp
        ../ui/process_action_worker.cpp ../ui/process_action_worker.h
        ../ui/full_scan_worker.cpp ../ui/full_scan_worker.h
        ../ui/full_scan_dialog.cpp ../ui/full_scan_dialog.h
        ../ui/deep_scan_worker.cpp ../ui/deep_scan_worker.h
        ../ui/scan_report_sections.cpp ../ui/scan_report_sections.h
        ../ui/deep_scan_dialog.cpp ../ui/deep_scan_dialog.h
        ../ui/startup_sequence.cpp
        ../ui/startup_sequence.h
        ../ui/main_window.cpp
        ../ui/main_window.h
        ../ui/diagnostic_dock_layout.cpp ../ui/diagnostic_dock_layout.h
        ../ui/telemetry_worker.cpp
        ../ui/telemetry_worker.h
        ../ui/ping_worker.cpp
        ../ui/ping_worker.h
        ../ui/network_scanner_worker.cpp
        ../ui/network_scanner_worker.h
        ../ui/network_identity.cpp
        ../ui/network_identity.h
        ../ui/internet_tools_worker.cpp
        ../ui/internet_tools_worker.h
        ../ui/overlay_fps_counter.cpp
        ../ui/overlay_fps_counter.h
        ../ui/server_status_checker.cpp
        ../ui/server_status_checker.h
        ../ui/terminal_text.cpp
        ../ui/terminal_text.h
        ../ui/process_worker.cpp
        ../ui/process_worker.h
        ../ui/autostart_worker.cpp
        ../ui/autostart_worker.h
        ../ui/diagnostic_worker.cpp
        ../ui/diagnostic_worker.h
        ../ui/deep_telemetry_dialog.cpp
        ../ui/deep_telemetry_dialog.h
        ../ui/stress_worker.cpp
        ../ui/stress_worker.h
        ../ui/app_monitor_worker.cpp
        ../ui/app_monitor_worker.h
        ../ui/incident_worker.cpp
        ../ui/app_process_session.cpp ../ui/app_process_session.h
        ../ui/incident_worker.h
        ../ui/hardware_inventory_worker.cpp
        ../ui/hardware_inventory_worker.h
        ../ui/hardware_inventory_details.cpp
        ../ui/hardware_inventory_details.h
        ../ui/widgets/metric_card.cpp
        ../ui/widgets/independent_dock_widget.cpp
        ../ui/widgets/independent_dock_widget.h
        ../ui/widgets/metric_card.h
        ../ui/widgets/details_panel.cpp
        ../ui/widgets/details_panel.h
        ../ui/widgets/gauge_widget.cpp
        ../ui/widgets/gauge_widget.h
        ../ui/widgets/gamer_overlay.cpp
        ../ui/widgets/gamer_overlay.h
        ../ui/widgets/sparkline_widget.cpp
        ../ui/widgets/sparkline_widget.h
        ../ui/widgets/cpu_core_chart_widget.cpp
        ../ui/widgets/cpu_core_chart_widget.h
        ../ui/widgets/network_traffic_chart.cpp
        ../ui/widgets/network_traffic_chart.h
        ../ui/widgets/app_monitor_chart.cpp
        ../ui/widgets/app_monitor_chart.h
        ../ui/widgets/server_monitor_widget.cpp
        ../ui/widgets/server_monitor_widget.h
        ../ui/widgets/terminal_widget.cpp
        ../ui/widgets/terminal_widget.h
    )
    set_target_properties(orion_ui_contract_tests PROPERTIES AUTOMOC ON)
    target_include_directories(orion_ui_contract_tests PRIVATE ../ui)
    target_link_libraries(orion_ui_contract_tests
        PRIVATE ORION::Platform ORION::Storage ORION::Diagnostics
            Qt6::Core Qt6::Gui Qt6::Network Qt6::Widgets)
    if(WIN32)
        target_link_libraries(orion_ui_contract_tests PRIVATE
            d3d11 d3dcompiler dxgi iphlpapi ws2_32 wbemuuid ole32 oleaut32)
    endif()
    add_test(NAME orion_ui_contract_tests COMMAND orion_ui_contract_tests)
    set_tests_properties(orion_ui_contract_tests PROPERTIES
        TIMEOUT 25
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        get_filename_component(_orion_ui_test_compiler_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(orion_ui_contract_tests PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${_orion_ui_test_compiler_runtime_dir}")
    endif()
endif()
