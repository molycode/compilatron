add_library(imgui_lib STATIC
	imgui.cpp
	imgui_demo.cpp
	imgui_draw.cpp
	imgui_tables.cpp
	imgui_widgets.cpp
	backends/imgui_impl_glfw.cpp
	backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_lib SYSTEM PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/backends
)

target_link_libraries(imgui_lib PRIVATE glfw)
target_compile_options(imgui_lib PRIVATE -fno-exceptions)
set_target_properties(imgui_lib PROPERTIES SYSTEM TRUE)
