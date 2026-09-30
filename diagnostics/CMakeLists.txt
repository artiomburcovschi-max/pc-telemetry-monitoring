# Включаем автоматическую генерацию MOC-файлов для Qt (необходимо для сигналов, слотов и метасистемы)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Описание статической библиотеки диагностики
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

# Линкуем зависимости к самой библиотеке
target_link_libraries(orion_diagnostics PUBLIC ORION::Core Qt6::Core)

if(WIN32)
    target_compile_definitions(orion_diagnostics PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(orion_diagnostics PRIVATE wevtapi)
    
    # Если сборка идет через GCC/MinGW, заставляем линкер читать всю библиотеку целиком
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(orion_diagnostics INTERFACE "-Wl,--whole-archive" "$<TARGET_FILE:orion_diagnostics>" "-Wl,--no-whole-archive")
    endif()
endif()

target_compile_features(orion_diagnostics PUBLIC cxx_std_20)

# Описание исполняемого файла утилиты (теста)
add_executable(orion_golden_contract golden_contract_main.cpp)

# ЖЕСТКАЯ СВЯЗКА ДЛЯ MinGW: Циклическая линковка через --start-group.
# Это заставит ld.exe гонять по кругу Diagnostics и Qt6::Core, 
# пока функции JSON из cpp-файла теста и подсистемы логов полностью не свяжутся.
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(orion_golden_contract PRIVATE 
        -Wl,--start-group
        ORION::Diagnostics 
        Qt6::Core
        -Wl,--end-group
    )
else()
    # Для MSVC в GitHub Actions оставляем стандартную цепочку
    target_link_libraries(orion_golden_contract PRIVATE 
        ORION::Diagnostics 
        Qt6::Core
    )
endif()

target_compile_definitions(orion_golden_contract PRIVATE
    ORION_DEFAULT_GOLDEN_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/diagnostic_golden_cases.json"
)

set_target_properties(orion_golden_contract PROPERTIES OUTPUT_NAME "orion-golden-contract")
