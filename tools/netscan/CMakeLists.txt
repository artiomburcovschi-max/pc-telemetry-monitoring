find_package(Threads REQUIRED)

add_executable(orion_netscan netscan.cpp)
target_link_libraries(orion_netscan PRIVATE Threads::Threads)
target_compile_features(orion_netscan PRIVATE cxx_std_20)

if(WIN32)
    target_compile_definitions(orion_netscan PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(orion_netscan PRIVATE iphlpapi ws2_32)
endif()

