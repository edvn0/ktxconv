cmake_minimum_required(VERSION 3.25)

project(
    ktxconv
    VERSION 0.1.0
    LANGUAGES C CXX
)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KTXCONV_BUILD_TOOLS "Build command line tools" ${PROJECT_IS_TOP_LEVEL})

if(NOT PROJECT_IS_TOP_LEVEL)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
endif()

message(STATUS "Building tools: ${KTXCONV_BUILD_TOOLS}, building tests: ${BUILD_TESTING}")

include(cmake/CPM.cmake)

find_package(VulkanHeaders CONFIG QUIET)
if(NOT VulkanHeaders_FOUND)
  CPMAddPackage(
        NAME Vulkan-Headers
        GITHUB_REPOSITORY KhronosGroup/Vulkan-Headers
        GIT_TAG vulkan-sdk-1.4.341.0
    )
endif()

CPMAddPackage(
    NAME ktx
    GITHUB_REPOSITORY KhronosGroup/KTX-Software
    VERSION 4.4.2
    OPTIONS
        "KTX_FEATURE_TESTS OFF"
        "KTX_FEATURE_TOOLS OFF"
        "KTX_FEATURE_VK_UPLOAD OFF"
        "KTX_FEATURE_GL_UPLOAD OFF"
)

CPMAddPackage(
    NAME stb
    GITHUB_REPOSITORY nothings/stb
    GIT_TAG master
    DOWNLOAD_ONLY YES
)

add_library(ktxconv
    src/image.cxx
    src/mip_chain.cxx
    src/format.cxx
    src/compression.cxx
    src/ktx2_write.cxx
    src/ktx2_decode.cxx
    src/stb.cxx
)

add_library(ktxconv::ktxconv ALIAS ktxconv)

target_include_directories(ktxconv
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${stb_SOURCE_DIR}
)

target_compile_features(ktxconv PUBLIC cxx_std_23)

target_link_libraries(ktxconv
    PUBLIC
        Vulkan::Headers
    PRIVATE
        ktx
)

if(KTXCONV_BUILD_TOOLS)
  CPMAddPackage(
        NAME lyra
        GITHUB_REPOSITORY bfgroup/Lyra
        GIT_TAG 1.7.0
    )

  add_library(ktxconv_encode_app
        src/app_encode.cxx
    )

  target_include_directories(ktxconv_encode_app
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

  target_link_libraries(ktxconv_encode_app
        PRIVATE
            ktxconv
            lyra
    )

  add_library(ktxconv_decode_app
        src/app_decode.cxx
    )

  target_include_directories(ktxconv_decode_app
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

  target_link_libraries(ktxconv_decode_app
        PRIVATE
            ktxconv
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
endif()

if(BUILD_TESTING)
  CPMAddPackage(
        NAME doctest
        GITHUB_REPOSITORY doctest/doctest
        VERSION 2.5.0
    )

  add_executable(ktxconv_tests
        tests/test_main.cxx
        tests/test_mip_chain.cxx
        tests/test_e2e.cxx
    )

  target_link_libraries(ktxconv_tests
        PRIVATE
            ktxconv
            ktxconv_encode_app
            ktxconv_encode_app
            doctest::doctest
    )

  target_compile_definitions(ktxconv_tests
        PRIVATE
            TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets"
    )

  include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
  doctest_discover_tests(ktxconv_tests)
endif()
