add_library(imgui_lib STATIC
	upstream/imgui.cpp
	upstream/imgui_demo.cpp
	upstream/imgui_draw.cpp
	upstream/imgui_tables.cpp
	upstream/imgui_widgets.cpp
	upstream/backends/imgui_impl_glfw.cpp
	upstream/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_lib SYSTEM PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/upstream
	${CMAKE_CURRENT_SOURCE_DIR}/upstream/backends
)

target_link_libraries(imgui_lib PRIVATE glfw)
target_compile_options(imgui_lib PRIVATE -fno-exceptions)
set_target_properties(imgui_lib PROPERTIES SYSTEM TRUE)
