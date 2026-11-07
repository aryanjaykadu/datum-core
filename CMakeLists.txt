cmake_minimum_required(VERSION 3.24)
project(datum_kernel CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(dk STATIC src/expr.cpp src/shapes.cpp src/mesh.cpp src/bricks.cpp src/document.cpp src/analysis.cpp src/robot.cpp src/sketch.cpp src/meshfield.cpp)
target_include_directories(dk PUBLIC include)
target_compile_options(dk PRIVATE -Wall -Wextra)

add_executable(dk_tests tests/test_main.cpp)
target_link_libraries(dk_tests PRIVATE dk)

add_executable(bracket examples/bracket.cpp)
target_link_libraries(bracket PRIVATE dk)

add_executable(export_scene examples/export_scene.cpp)
target_link_libraries(export_scene PRIVATE dk)

# the sidecar the desktop shell talks to over stdio
add_executable(dk_engine engine/main.cpp)
target_link_libraries(dk_engine PRIVATE dk)
if(WIN32)
  target_link_options(dk_engine PRIVATE -static)
endif()

enable_testing()
add_test(NAME dk_tests COMMAND dk_tests)
