
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build the GLFW example programs")
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build the GLFW test programs")
set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build the GLFW documentation")
set(GLFW_INSTALL OFF CACHE BOOL "Generate installation target")
add_subdirectory(GLFW)

set(GLM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/glm PARENT_SCOPE)
set(STB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/stb PARENT_SCOPE)
set(IMGUI_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

add_library(imgui STATIC
    "${IMGUI_INCLUDE_DIR}/imgui.cpp"
    "${IMGUI_INCLUDE_DIR}/imgui_draw.cpp"
    "${IMGUI_INCLUDE_DIR}/imgui_tables.cpp"
    "${IMGUI_INCLUDE_DIR}/imgui_widgets.cpp"
    "${IMGUI_INCLUDE_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_INCLUDE_DIR}/backends/imgui_impl_vulkan.cpp"
)

target_include_directories(imgui PUBLIC
    "${IMGUI_INCLUDE_DIR}"
    "${IMGUI_INCLUDE_DIR}/backends"
)

target_link_libraries(imgui PUBLIC Vulkan::Vulkan glfw)

ExternalTarget("external" imgui)