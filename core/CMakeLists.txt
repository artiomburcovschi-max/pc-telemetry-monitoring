add_library(orion_core STATIC
    src/cpu_samples.cpp
    src/memory_pressure.cpp
    src/process_tree_counters.cpp
    src/metric_window.cpp
    src/session_peaks.cpp
    src/thresholds.cpp
)

add_library(ORION::Core ALIAS orion_core)

target_include_directories(orion_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(orion_core PUBLIC cxx_std_20)
