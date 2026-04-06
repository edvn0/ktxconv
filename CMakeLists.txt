cmake_minimum_required(VERSION 3.27)
project(ktxconv LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/CPM.cmake)

CPMAddPackage(
    NAME doctest
    GITHUB_REPOSITORY doctest/doctest
    VERSION 2.5.0
)

CPMAddPackage(
    NAME lyra
    GITHUB_REPOSITORY bfgroup/Lyra
    GIT_TAG 1.7.0
)

CPMAddPackage(
    NAME KTX
    GITHUB_REPOSITORY KhronosGroup/KTX-Software
    VERSION 4.4.2
    OPTIONS
        "KTX_FEATURE_TESTS OFF"
        "KTX_FEATURE_TOOLS OFF"
        "KTX_FEATURE_VK_UPLOAD OFF"
        "KTX_FEATURE_GL_UPLOAD OFF"
)

CPMAddPackage(
    NAME Vulkan-Headers
    GITHUB_REPOSITORY KhronosGroup/Vulkan-Headers
    GIT_TAG vulkan-sdk-1.4.341.0
)

CPMAddPackage(
    NAME stb
    GITHUB_REPOSITORY nothings/stb
    GIT_TAG master
    DOWNLOAD_ONLY YES
)

add_library(ktxconv_3pp STATIC
    src/stb.cxx
)

target_include_directories(ktxconv_3pp
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC
        ${stb_SOURCE_DIR}
)

add_library(ktxconv_core STATIC
    src/image.cxx
    src/mip_chain.cxx
    src/format.cxx
    src/compression.cxx
    src/ktx2_write.cxx
    src/ktx2_decode.cxx
)

target_include_directories(ktxconv_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ktxconv_core
    PUBLIC
        ktx
        ktxconv_3pp
        Vulkan::Headers
)

add_library(ktxconv_encode_app STATIC
    src/app_encode.cxx
)

target_include_directories(ktxconv_encode_app
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ktxconv_encode_app
    PRIVATE
        ktxconv_core
        lyra
)

add_library(ktxconv_decode_app STATIC
    src/app_decode.cxx
)

target_include_directories(ktxconv_decode_app
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ktxconv_decode_app
    PRIVATE
        ktxconv_core
        lyra
)

add_executable(ktxconv_encode
    src/main_encode.cxx
)

target_link_libraries(ktxconv_encode
    PRIVATE
        ktxconv_encode_app
)

add_executable(ktxconv_decode
    src/main_decode.cxx
)

target_link_libraries(ktxconv_decode
    PRIVATE
        ktxconv_decode_app
)

add_executable(ktxconv_tests
    tests/test_main.cxx
    tests/test_mip_chain.cxx
    tests/test_e2e.cxx
)

target_link_libraries(ktxconv_tests
    PRIVATE
        ktxconv_core
        ktxconv_encode_app
        ktxconv_decode_app
        doctest::doctest
)

target_compile_definitions(ktxconv_tests PRIVATE
    TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets"
)

enable_testing()
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

doctest_discover_tests(ktxconv_tests)