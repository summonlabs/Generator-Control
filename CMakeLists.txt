cmake_minimum_required(VERSION 3.25)
project(GeneratorControl VERSION 1.0.0
        DESCRIPTION "Generator Control - vendor-neutral standby generator control and lifecycle runtime"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
enable_testing()

option(GC_BUILD_TESTS "Build the test suite" ON)
option(GC_BUILD_EXAMPLES "Build the runnable examples" ON)
option(GC_BUILD_BENCHMARKS "Build the benchmark executable" ON)
option(GC_BUILD_CLI "Build the generatorctl CLI" ON)
option(GC_ENABLE_ASAN "Enable AddressSanitizer for first-party targets" OFF)

# ---------------------------------------------------------------------------
# First-party warning policy: strict, and never globally suppressed.
# ---------------------------------------------------------------------------
function(gc_target_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

function(gc_target_sanitizers target)
  if(GC_ENABLE_ASAN)
    if(MSVC)
      target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
      target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    else()
      target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
      target_link_options(${target} PRIVATE -fsanitize=address)
    endif()
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
set(GC_SOURCES
  src/result.cpp
  src/ids.cpp
  src/time.cpp
  src/units.cpp
  src/digest.cpp
  src/canonical.cpp
  src/evidence.cpp
  src/state.cpp
  src/readiness.cpp
  src/resource.cpp
  src/sync.cpp
  src/transfer.cpp
  src/authority.cpp
  src/adapter.cpp
  src/synthetic_adapter.cpp
  src/attempt.cpp
  src/model.cpp
  src/path_safety.cpp
  src/platform_win32.cpp
  src/persistence.cpp
  src/engine.cpp)

add_library(generatorcontrol STATIC ${GC_SOURCES})
add_library(GeneratorControl::generatorcontrol ALIAS generatorcontrol)
gc_target_warnings(generatorcontrol)
gc_target_sanitizers(generatorcontrol)
target_include_directories(generatorcontrol
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(generatorcontrol PUBLIC cxx_std_20)

find_package(Threads REQUIRED)
target_link_libraries(generatorcontrol PUBLIC Threads::Threads)

set_target_properties(generatorcontrol PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Test suite
# ---------------------------------------------------------------------------
if(GC_BUILD_TESTS)
  file(GLOB GC_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
  add_executable(generatorcontrol_tests ${GC_TEST_SOURCES})
  gc_target_warnings(generatorcontrol_tests)
  gc_target_sanitizers(generatorcontrol_tests)
  target_include_directories(generatorcontrol_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(generatorcontrol_tests PRIVATE GeneratorControl::generatorcontrol)
  add_test(NAME generatorcontrol_tests COMMAND generatorcontrol_tests)
endif()

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
if(GC_BUILD_CLI)
  file(GLOB GC_CLI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp)
  add_executable(generatorcontrol_cli ${GC_CLI_SOURCES})
  set_target_properties(generatorcontrol_cli PROPERTIES OUTPUT_NAME generatorctl)
  gc_target_warnings(generatorcontrol_cli)
  gc_target_sanitizers(generatorcontrol_cli)
  target_include_directories(generatorcontrol_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(generatorcontrol_cli PRIVATE GeneratorControl::generatorcontrol)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(GC_BUILD_EXAMPLES)
  file(GLOB GC_EXAMPLE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp)
  foreach(src ${GC_EXAMPLE_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(example_${name} ${src})
    gc_target_warnings(example_${name})
    gc_target_sanitizers(example_${name})
    target_include_directories(example_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(example_${name} PRIVATE GeneratorControl::generatorcontrol)
  endforeach()
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
if(GC_BUILD_BENCHMARKS)
  file(GLOB GC_BENCH_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cpp)
  add_executable(generatorcontrol_bench ${GC_BENCH_SOURCES})
  gc_target_warnings(generatorcontrol_bench)
  gc_target_sanitizers(generatorcontrol_bench)
  target_include_directories(generatorcontrol_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(generatorcontrol_bench PRIVATE GeneratorControl::generatorcontrol)
endif()

# ---------------------------------------------------------------------------
# Install and export
# ---------------------------------------------------------------------------
install(TARGETS generatorcontrol
        EXPORT GeneratorControlTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT GeneratorControlTargets
        FILE GeneratorControlTargets.cmake
        NAMESPACE GeneratorControl::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GeneratorControl)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GeneratorControlConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/GeneratorControlConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GeneratorControl)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/GeneratorControlConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/GeneratorControlConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/GeneratorControlConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GeneratorControl)
install(FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
  ${CMAKE_CURRENT_SOURCE_DIR}/NOTICE
  DESTINATION ${CMAKE_INSTALL_DATADIR}/GeneratorControl)
