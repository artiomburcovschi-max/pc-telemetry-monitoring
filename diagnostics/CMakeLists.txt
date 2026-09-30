add_library(orion_diagnostics STATIC
    src/diagnostic_engine.cpp
    src/diagnostic_schema.cpp
    src/report_contract.cpp
    src/runtime_diagnostics.cpp
    src/app_monitor_data.cpp
    src/incident_data.cpp
    src/system_diagnostics_collector.cpp
    src/system_log_limits.cpp
    src/telemetry_json.cpp
)

add_library(ORION::Diagnostics ALIAS orion_diagnostics)

target_include_directories(orion_diagnostics
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(orion_diagnostics PUBLIC ORION::Core Qt6::Core)
if(WIN32)
    target_compile_definitions(orion_diagnostics PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(orion_diagnostics PRIVATE wevtapi)
endif()
target_compile_features(orion_diagnostics PUBLIC cxx_std_20)

add_executable(orion_golden_contract golden_contract_main.cpp)
target_link_libraries(orion_golden_contract PRIVATE ORION::Diagnostics)
target_compile_definitions(orion_golden_contract PRIVATE
    ORION_DEFAULT_GOLDEN_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/diagnostic_golden_cases.json"
)
set_target_properties(orion_golden_contract PROPERTIES OUTPUT_NAME "orion-golden-contract")
