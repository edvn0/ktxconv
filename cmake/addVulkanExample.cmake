function(add_vulkan_compute_example target_name)
    set(options)
    set(one_value_args VKBOOTSTRAP_GIT_TAG VMA_GIT_TAG)
    set(multi_value_args SOURCES)
    cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if (NOT ARG_SOURCES)
        message(FATAL_ERROR "SOURCES required")
    endif()

    if (NOT ARG_VKBOOTSTRAP_GIT_TAG)
        set(ARG_VKBOOTSTRAP_GIT_TAG ffa9645)
    endif()

    if (NOT ARG_VMA_GIT_TAG)
        set(ARG_VMA_GIT_TAG HEAD)
    endif()

    if (NOT ARG_VOLK_GIT_TAG)
        set(ARG_VOLK_GIT_TAG 1.4.304)
    endif()

    CPMAddPackage(
        NAME vk-bootstrap
        GITHUB_REPOSITORY charles-lunarg/vk-bootstrap
        GIT_TAG ${ARG_VKBOOTSTRAP_GIT_TAG}
    )
    
    CPMAddPackage(
        NAME volk
        GITHUB_REPOSITORY zeux/volk
        GIT_TAG ${ARG_VOLK_GIT_TAG}
    )

    CPMAddPackage(
        NAME VulkanMemoryAllocator
        GITHUB_REPOSITORY GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
        GIT_TAG ${ARG_VMA_GIT_TAG}
    )

    add_executable(${target_name} )
    target_sources(${target_name} PRIVATE ${ARG_SOURCES})
    target_compile_features(${target_name} PRIVATE cxx_std_20)

    find_package(PNG REQUIRED)

    target_link_libraries(${target_name}
        PRIVATE
            vk-bootstrap::vk-bootstrap
            volk::volk
            GPUOpen::VulkanMemoryAllocator
            PNG::PNG
            ${CMAKE_DL_LIBS}
    )

    if (UNIX AND NOT APPLE)
        target_link_libraries(${target_name} PRIVATE ${CMAKE_DL_LIBS})
    endif()
endfunction()