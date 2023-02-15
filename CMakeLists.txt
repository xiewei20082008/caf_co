cmake_minimum_required(VERSION 3.25)
project (caf_co_test)

set(CMAKE_CXX_STANDARD 20)

add_compile_options("-stdlib=libc++")
set(CMAKE_CXX_FLAGS "-fcoroutines-ts")

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_executable (caf_co_test main.cpp)

target_link_libraries (caf_co_test ${CONAN_LIBS} libc++.a libc++abi.a)