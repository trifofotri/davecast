cmake_minimum_required(VERSION 3.6)
project(davecast C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

file(GLOB SOURCES src/*.c)

add_library(davecast ${SOURCES} third_party/cjson/cJSON.c)

target_include_directories(davecast PUBLIC include third_party)
target_link_libraries(davecast PUBLIC CURL::libcurl OpenSSL::SSL OpenSSL::Crypto)

# libdave: prebuilt externally btw so yeah
set(LIBDAVE_DIR "${CMAKE_SOURCE_DIR}/third_party/libdave" CACHE PATH "libdave checkout")
set(LIBDAVE_BUILD_DIR "${LIBDAVE_DIR}/cpp/build")
set(LIBDAVE_VCPKG_LIB_DIR "${LIBDAVE_BUILD_DIR}/vcpkg_installed/x64-linux/lib")

if(NOT EXISTS "${LIBDAVE_BUILD_DIR}/libdave.a")
    message(FATAL_ERROR "libdave.a not found under ${LIBDAVE_BUILD_DIR} -- build it first:\n"
        "  cd ${LIBDAVE_DIR}/cpp && make")
endif()

target_include_directories(davecast PUBLIC ${LIBDAVE_DIR}/cpp/includes)

target_link_libraries(davecast PUBLIC
    ${LIBDAVE_BUILD_DIR}/libdave.a
    ${LIBDAVE_VCPKG_LIB_DIR}/libmlspp.a
    ${LIBDAVE_VCPKG_LIB_DIR}/libmls_ds.a
    ${LIBDAVE_VCPKG_LIB_DIR}/libhpke.a
    ${LIBDAVE_VCPKG_LIB_DIR}/libbytes.a
    ${LIBDAVE_VCPKG_LIB_DIR}/libtls_syntax.a
    stdc++
    dl
    pthread
)

add_subdirectory(examples)