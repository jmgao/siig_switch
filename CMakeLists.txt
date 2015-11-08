cmake_minimum_required(VERSION 2.811)
project(siig_switch)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
find_package(libusb-1.0 REQUIRED)
include_directories(${LIBUSB_1_INCLUDE_DIRS})

set(SRCS siig_switch.cpp)
add_executable(siig_switch ${SRCS})
target_link_libraries(siig_switch ${LIBUSB_1_LIBRARIES})

set(CMAKE_CXX_FLAGS "-std=c++14 -Wall -Wextra -Werror")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcolor-diagnostics=always")
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always")
endif()

