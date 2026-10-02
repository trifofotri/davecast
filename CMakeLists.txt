cmake_minimum_required(VERSION 3.6)
project(davecast C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL REQUIRED)

file(GLOB SOURCES src/*.c)

add_library(davecast ${SOURCES} third_party/cjson/cJSON.c)

target_include_directories(davecast PUBLIC include third_party)
target_link_libraries(davecast PUBLIC CURL::libcurl)

add_subdirectory(examples)