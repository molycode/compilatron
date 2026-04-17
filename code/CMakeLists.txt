file(GLOB_RECURSE Compilatron_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/**/*.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/**/*.hpp
)

add_executable(Compilatron
	${Compilatron_SOURCES}
	${RESOURCE_FILES}
)

# Include directories — SYSTEM on external paths hides them from the IDE's header file list
target_include_directories(Compilatron PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(Compilatron SYSTEM PRIVATE
	${IMGUI_DIR}
	${IMGUI_DIR}/backends
	${CMAKE_SOURCE_DIR}/external/fonts
)

# Copy font files alongside the executable
set(FONTS_OUTPUT_DIR ${CMAKE_BINARY_DIR}/ctrn/fonts)
add_custom_command(TARGET Compilatron POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory ${FONTS_OUTPUT_DIR}
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		${CMAKE_SOURCE_DIR}/external/fonts/fa-solid-900.ttf
		${FONTS_OUTPUT_DIR}/fa-solid-900.ttf
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		${CMAKE_SOURCE_DIR}/external/imgui/misc/fonts/Roboto-Medium.ttf
		${FONTS_OUTPUT_DIR}/Roboto-Medium.ttf
	COMMENT "Copying fonts to build output"
)

if(STATIC_LINKING AND NOT MSVC)
	target_link_options(Compilatron PRIVATE -static-libgcc -static-libstdc++)
endif()

target_link_libraries(Compilatron
	imgui_lib
	TgeBase
	TgeLogging
	Threads::Threads
	glfw
	GL
	${CMAKE_DL_LIBS}
)

if(PLATFORM_WINDOWS)
	target_link_libraries(Compilatron ws2_32 opengl32 gdi32)
endif()

target_compile_definitions(Compilatron PRIVATE
	GUI_AVAILABLE=1
	IMGUI_IMPL_OPENGL_LOADER_GL3W=0
	CTRN_VERSION="${PROJECT_VERSION}"
	$<$<BOOL:${PLATFORM_WINDOWS}>:CTRN_PLATFORM_WINDOWS>
	$<$<BOOL:${PLATFORM_LINUX}>:CTRN_PLATFORM_LINUX>
	$<$<BOOL:${PLATFORM_MACOS}>:CTRN_PLATFORM_MACOS>
)

# Place the binary at the build root regardless of CMakeLists.txt nesting
set_target_properties(Compilatron PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
	OUTPUT_NAME compilatron
)
