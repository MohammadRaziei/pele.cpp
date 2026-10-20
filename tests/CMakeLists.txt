add_executable(test_layered test_layered.cpp)
target_link_libraries(test_layered PRIVATE pele)
target_include_directories(test_layered PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME layered COMMAND test_layered ${CMAKE_SOURCE_DIR}/tests/data/layered_cases.txt)
