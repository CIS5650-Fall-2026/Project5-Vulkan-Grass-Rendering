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

source_group("Shaders" FILES ${SHADER_SOURCES})

if(WIN32)
    add_executable(vulkan_grass_rendering WIN32 ${SOURCES} ${SHADER_SOURCES})
    target_link_libraries(vulkan_grass_rendering ${WINLIBS})
else(WIN32)
    add_executable(vulkan_grass_rendering ${SOURCES})
    target_link_libraries(vulkan_grass_rendering ${CMAKE_THREAD_LIBS_INIT})
endif(WIN32)

find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" "$ENV{VK_SDK_PATH}/Bin")
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator was not found. Set VULKAN_SDK to your Vulkan SDK directory.")
endif()

set(SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
foreach(SHADER_SOURCE ${SHADER_SOURCES})
    get_filename_component(fname ${SHADER_SOURCE} NAME)
    add_custom_command(OUTPUT ${SHADER_DIR}/${fname}.spv
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_DIR}
        COMMAND "${GLSLANG_VALIDATOR}" -V ${SHADER_SOURCE} -o ${SHADER_DIR}/${fname}.spv -g
        DEPENDS ${SHADER_SOURCE}
        VERBATIM
    )
    list(APPEND SHADER_BINARIES ${SHADER_DIR}/${fname}.spv)
endforeach()
add_custom_target(grass_shaders DEPENDS ${SHADER_BINARIES})
ExternalTarget("Shaders" grass_shaders)
add_dependencies(vulkan_grass_rendering grass_shaders)

set_property(TARGET vulkan_grass_rendering PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(vulkan_grass_rendering ${ASSIMP_LIBRARIES} Vulkan::Vulkan glfw)
target_include_directories(vulkan_grass_rendering PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${GLM_INCLUDE_DIR}
  ${STB_INCLUDE_DIR}
)

InternalTarget("" vulkan_grass_rendering)
