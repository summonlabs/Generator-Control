# Independent out-of-tree consumer of the installed Generator Control package.
#
# This project is deliberately not part of the main build. It is configured against
# an installed prefix and consumes the exported CMake target exactly as an external
# project would, so a broken install or export fails here rather than silently
# working inside the build tree.
cmake_minimum_required(VERSION 3.25)
project(GeneratorControlDownstream LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(GeneratorControl 1.0.0 REQUIRED)

add_executable(downstream_consumer main.cpp)
if(MSVC)
  target_compile_options(downstream_consumer PRIVATE /W4 /WX /permissive- /utf-8)
else()
  target_compile_options(downstream_consumer PRIVATE -Wall -Wextra -Werror)
endif()
target_link_libraries(downstream_consumer PRIVATE GeneratorControl::generatorcontrol)
