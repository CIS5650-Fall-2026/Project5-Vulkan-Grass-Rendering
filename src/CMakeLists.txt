file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

file(GLOB IMAGES
    ${CMAKE_CURRENT_SOURCE_DIR}/images/*.jpg
    ${CMAKE_CURRENT_SOURCE_DIR}/images/*.png
    ${CMAKE_CURRENT_SOURCE_DIR}/images/*.bmp
)

foreach(IMAGE ${IMAGES})
    get_filename_component(fname ${IMAGE} NAME)
    configure_file(${IMAGE} ${CMAKE_CURRENT_BINARY_DIR}/images/${fname} COPYONLY)
endforeach()

file(GLOB_RECURSE SHADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/*.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/*.geom
    ${CMAKE_CURRENT_SOURCE_DIR}/*.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.tese
    ${CMAKE_CURRENT_SOURCE_DIR}/*.tesc
)

file(GLOB SLANG_SHADER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.slang)

source_group("Shaders" FILES ${SHADER_SOURCES} ${SLANG_SHADER_SOURCES})

if(WIN32)
    add_executable(vulkan_grass_rendering WIN32 ${SOURCES} ${SHADER_SOURCES} ${SLANG_SHADER_SOURCES})
    target_link_libraries(vulkan_grass_rendering ${WINLIBS})
else(WIN32)
    add_executable(vulkan_grass_rendering ${SOURCES})
    target_link_libraries(vulkan_grass_rendering ${CMAKE_THREAD_LIBS_INIT})
endif(WIN32)

foreach(SHADER_SOURCE ${SHADER_SOURCES})
    set(SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

    if(WIN32)
        get_filename_component(fname ${SHADER_SOURCE} NAME)
        add_custom_target(${fname}.spv
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_DIR} && 
            $ENV{VK_SDK_PATH}/Bin/glslangValidator.exe -V ${SHADER_SOURCE} -o ${SHADER_DIR}/${fname}.spv -g
            SOURCES ${SHADER_SOURCE}
        )
        ExternalTarget("Shaders" ${fname}.spv)
        add_dependencies(vulkan_grass_rendering ${fname}.spv)
    endif(WIN32)

    # TODO: Build shaders on not windows
endforeach()

# Slang shaders: each .slang file is compiled to a single SPIR-V module containing all of its entry points,
# named after the file without its extension (e.g. tessellation.slang -> shaders/tessellation.spv)
find_program(SLANGC_EXECUTABLE slangc
    HINTS $ENV{VULKAN_SDK}/Bin $ENV{VK_SDK_PATH}/Bin $ENV{VULKAN_SDK}/bin
)
if(SLANG_SHADER_SOURCES AND NOT SLANGC_EXECUTABLE)
    message(FATAL_ERROR "slangc not found; install the Vulkan SDK or set SLANGC_EXECUTABLE")
endif()

foreach(SHADER_SOURCE ${SLANG_SHADER_SOURCES})
    set(SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    get_filename_component(fname ${SHADER_SOURCE} NAME)
    get_filename_component(fname_we ${SHADER_SOURCE} NAME_WE)
    add_custom_target(${fname}.spv
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_DIR}
        COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCE} -target spirv -fvk-use-entrypoint-name -g -o ${SHADER_DIR}/${fname_we}.spv
        SOURCES ${SHADER_SOURCE}
    )
    ExternalTarget("Shaders" ${fname}.spv)
    add_dependencies(vulkan_grass_rendering ${fname}.spv)
endforeach()

target_link_libraries(vulkan_grass_rendering ${ASSIMP_LIBRARIES} Vulkan::Vulkan glfw)
target_include_directories(vulkan_grass_rendering PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${GLM_INCLUDE_DIR}
  ${STB_INCLUDE_DIR}
)

InternalTarget("" vulkan_grass_rendering)
