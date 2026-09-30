add_executable(orion_probe main.cpp)
target_link_libraries(orion_probe PRIVATE ORION::Platform)
set_target_properties(orion_probe PROPERTIES OUTPUT_NAME "orion-probe")

