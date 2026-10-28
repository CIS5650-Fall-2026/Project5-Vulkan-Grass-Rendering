set(renderer_sources
    main.cpp
    Renderer.cpp
    Scene.cpp
)

set(shader_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(shader_output_dir "${CMAKE_BINARY_DIR}/bin/shaders")

file(GLOB shader_sources CONFIGURE_DEPENDS
    "${shader_source_dir}/*.comp"
    "${shader_source_dir}/*.frag"
    "${shader_source_dir}/*.geom"
    "${shader_source_dir}/*.tesc"
    "${shader_source_dir}/*.tese"
    "${shader_source_dir}/*.vert"
)

if(DEFINED Vulkan_GLSLANG_VALIDATOR_EXECUTABLE
   AND EXISTS "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")
    set(glslang_validator "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")
else()
    find_program(glslang_validator
        NAMES glslangValidator glslangValidator.exe
        HINTS
            "$ENV{VULKAN_SDK}/bin"
            "$ENV{VULKAN_SDK}/Bin"
            "$ENV{VK_SDK_PATH}/bin"
            "$ENV{VK_SDK_PATH}/Bin"
        DOC "Path to the Vulkan SDK glslangValidator executable"
    )
endif()

if(NOT glslang_validator)
    message(FATAL_ERROR
        "glslangValidator was not found. Install the Vulkan SDK or add its bin "
        "directory to PATH (VULKAN_SDK and VK_SDK_PATH are also searched).")
endif()

set(shader_outputs)
foreach(shader_source IN LISTS shader_sources)
    get_filename_component(shader_name "${shader_source}" NAME)
    set(shader_output "${shader_output_dir}/${shader_name}.spv")

    add_custom_command(
        OUTPUT "${shader_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${shader_output_dir}"
        COMMAND "${glslang_validator}"
                -V
                --target-env vulkan1.1
                -o "${shader_output}"
                "${shader_source}"
        DEPENDS "${shader_source}"
        COMMENT "Compiling ${shader_name}"
        VERBATIM
    )

    list(APPEND shader_outputs "${shader_output}")
endforeach()

add_custom_target(vulkan_grass_shaders ALL DEPENDS ${shader_outputs})
set_target_properties(vulkan_grass_shaders PROPERTIES FOLDER "Shaders")

add_executable(vulkan_grass_rendering ${renderer_sources} ${shader_sources})
add_dependencies(vulkan_grass_rendering vulkan_grass_shaders)

target_link_libraries(vulkan_grass_rendering PRIVATE
    Vulkan::Vulkan
    glfw
)

target_include_directories(vulkan_grass_rendering PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/external/GLFW/include"
    "${GLM_INCLUDE_DIR}"
    "${STB_INCLUDE_DIR}"
)

target_compile_definitions(vulkan_grass_rendering PRIVATE
    NOMINMAX
    _USE_MATH_DEFINES
    _CRT_SECURE_NO_WARNINGS
    VULKAN_GRASS_SHADER_DIR=\"${shader_output_dir}\"
    VULKAN_GRASS_BUILD_TYPE=\"$<CONFIG>\"
    VULKAN_GRASS_SOURCE_COMMIT=\"${VULKAN_GRASS_SOURCE_COMMIT}\"
)

if(MSVC)
    target_compile_options(vulkan_grass_rendering PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(vulkan_grass_rendering PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

source_group("Source Files" FILES ${renderer_sources})
source_group("Shaders" FILES ${shader_sources})
set_target_properties(vulkan_grass_rendering PROPERTIES
    FOLDER "Application"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(vulkan_grass_rendering PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/bin"
    )
endforeach()
