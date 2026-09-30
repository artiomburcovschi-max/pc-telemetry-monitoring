add_library(orion_storage STATIC
    src/app_paths.cpp
    src/app_settings.cpp
    src/logging.cpp
    src/telemetry_files.cpp
    src/server_profile_store.cpp
)

add_library(ORION::Storage ALIAS orion_storage)

target_include_directories(orion_storage
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(orion_storage PUBLIC Qt6::Core Qt6::Sql)
target_compile_features(orion_storage PUBLIC cxx_std_20)
