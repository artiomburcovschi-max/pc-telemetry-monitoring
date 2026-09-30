add_library(orion_platform STATIC)
add_library(ORION::Platform ALIAS orion_platform)

target_sources(orion_platform
    PRIVATE
        src/system_backend_factory.cpp
        src/disk_collector_common.cpp
)

if(WIN32)
    target_sources(orion_platform PRIVATE
        src/windows_system_backend.cpp
        src/windows_disk_collector.cpp
        src/windows_gpu_collector.cpp
        src/windows_sensor_collector.cpp
        src/windows_process_collector.cpp
        src/windows_autostart_collector.cpp)
    target_compile_definitions(orion_platform PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(orion_platform PRIVATE
        iphlpapi
        advapi32
        dxgi
        dxguid
        wbemuuid
        ole32
        oleaut32
        psapi
        pdh
    )
elseif(UNIX AND NOT APPLE)
    target_sources(orion_platform PRIVATE
        src/linux_system_backend.cpp
        src/linux_disk_collector.cpp
        src/linux_gpu_collector.cpp
        src/linux_sensor_collector.cpp
        src/linux_process_collector.cpp
        src/linux_autostart_collector.cpp)
    target_link_libraries(orion_platform PRIVATE dl)
else()
    message(FATAL_ERROR "ORION currently supports Windows and Linux desktop targets")
endif()

target_include_directories(orion_platform
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(orion_platform PUBLIC ORION::Core)
target_compile_features(orion_platform PUBLIC cxx_std_20)
