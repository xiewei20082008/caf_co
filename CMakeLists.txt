cmake_minimum_required(VERSION 3.25)
project (caf_co_test)

set(CMAKE_CXX_STANDARD 20)
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options("-stdlib=libc++")

add_compile_options(-fsanitize=address)
add_link_options(-fsanitize=address)

find_package(CAF)


add_executable (caf_co_test main.cpp)

target_link_libraries (caf_co_test ${CONAN_LIBS} caf::caf c++ )