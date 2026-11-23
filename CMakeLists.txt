cmake_minimum_required(VERSION 3.20)
project(rosc VERSION 0.1.0 LANGUAGES C CXX)

option(ROSC_BUILD_EXAMPLES "Build examples" ON)
option(ROSC_BUILD_TESTS "Build tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Threads REQUIRED)
include(GNUInstallDirs)

add_library(rosc_objects OBJECT src/io_loop.cpp src/net.cpp src/node.cpp src/tcpros.cpp src/xmlrpc.cpp)
set_target_properties(rosc_objects PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)
target_include_directories(rosc_objects PUBLIC include)
target_compile_options(rosc_objects PRIVATE -Wall -Wextra)

foreach(kind SHARED STATIC)
  set(t rosc)
  if(kind STREQUAL STATIC)
    set(t rosc_static)
  endif()
  add_library(${t} ${kind} $<TARGET_OBJECTS:rosc_objects>)
  add_library(rosc::${t} ALIAS ${t})
  target_include_directories(${t} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_link_libraries(${t} PUBLIC Threads::Threads)
endforeach()
set_target_properties(rosc PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
set_target_properties(rosc_static PROPERTIES OUTPUT_NAME rosc)

install(TARGETS rosc rosc_static EXPORT rosc-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/rosc DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT rosc-targets NAMESPACE rosc:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rosc)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/rosc-config.cmake
     "include(CMakeFindDependencyMacro)\nfind_dependency(Threads)\ninclude(\${CMAKE_CURRENT_LIST_DIR}/rosc-targets.cmake)\n")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rosc-config.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rosc)

if(ROSC_BUILD_EXAMPLES)
  foreach(ex talker listener)
    add_executable(rosc_${ex} examples/${ex}.c)
    target_link_libraries(rosc_${ex} PRIVATE rosc_static)
    target_compile_options(rosc_${ex} PRIVATE -Wall -Wextra)
  endforeach()
endif()

if(ROSC_BUILD_TESTS)
  enable_testing()
  add_executable(rosc_unit_test tests/unit_test.cpp)
  target_include_directories(rosc_unit_test PRIVATE src)
  target_link_libraries(rosc_unit_test PRIVATE rosc_static)
  add_test(NAME unit COMMAND rosc_unit_test)
endif()
