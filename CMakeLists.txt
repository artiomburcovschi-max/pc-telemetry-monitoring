
add_executable(orion_netscan core/netscan/netscan.cpp)

if(WIN32)
    target_link_libraries(orion_netscan iphlpapi ws2_32)
endif()