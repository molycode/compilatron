cmake_minimum_required(VERSION 4.3.0)
project(Compilatron VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(${CMAKE_CURRENT_SOURCE_DIR}/external/tge-core/cmake/external.cmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(NOT CYGWIN)
	set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# Enforce minimum compiler versions required for C++23 (std::format, std::expected)
if(CMAKE_CXX_COMPILER_ID MATCHES "[Cc]lang")
	if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "19")
		message(FATAL_ERROR
			"Clang ${CMAKE_CXX_COMPILER_VERSION} is too old.\n"
			"Compilatron requires Clang 19 or newer.\n"
			"Reason: Clang 18 and earlier report __cpp_concepts=201907L, which prevents\n"
			"        libstdc++ 13 from exposing std::expected (requires >= 202002L).\n"
			"Tip: use CTRN_CLANG_PATH to point to a newer Clang installation.")
	endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13")
		message(FATAL_ERROR
			"GCC ${CMAKE_CXX_COMPILER_VERSION} is too old.\n"
			"Compilatron requires GCC 13 or newer.\n"
			"Reason: std::format is not available before GCC 13.\n"
			"Tip: use CTRN_GCC_PATH to point to a newer GCC installation.")
	endif()
endif()

# Compiler flags (warnings, pedantic, etc.)
if(CMAKE_CXX_COMPILER_ID MATCHES "[Cc]lang")
	include(cmake/compilers/clang.cmake)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	include(cmake/compilers/gcc.cmake)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

message(STATUS "CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_C_COMPILER = ${CMAKE_C_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER = ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER_ID = ${CMAKE_CXX_COMPILER_ID}")

# Build options
option(FORCE_GUI_BUILD "Force GUI build (fail if dependencies missing)" ON)
option(STATIC_LINKING "Enable static linking of C++ runtime libraries" OFF)

# Platform detection
if(WIN32)
	set(PLATFORM_WINDOWS TRUE)
elseif(UNIX AND NOT APPLE)
	set(PLATFORM_LINUX TRUE)
elseif(APPLE)
	set(PLATFORM_MACOS TRUE)
endif()

# Static linking configuration
if(STATIC_LINKING AND NOT MSVC)
	message(STATUS "Static linking enabled for C++ runtime libraries")
endif()

# tge-core integration (TgeBase + TgeLogging)
# TGE_LOGGING_ENABLED must be defined here — tge-core only adds it when it is the top-level project
add_compile_definitions(TGE_LOGGING_ENABLED)
tge_add_external(external/tge-core EXCLUDE_FROM_ALL)

# GLFW (vendored) — must precede imgui since imgui_impl_glfw needs its headers
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
tge_add_external(external/glfw EXCLUDE_FROM_ALL)

tge_add_external(external/imgui EXCLUDE_FROM_ALL)

# Debug logging - only enable for Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_compile_definitions(CTRN_ENABLE_DEBUG_LOGGING)
	message(STATUS "Debug logging enabled - logs will be written to debug.log")
else()
	message(STATUS "Debug logging disabled for ${CMAKE_BUILD_TYPE} build")
endif()

# Helper function to check if GUI dependencies are available
function(check_gui_dependencies)
	set(GUI_DEPENDENCIES_AVAILABLE FALSE PARENT_SCOPE)

	# GLFW is vendored — no check needed
	# X11/Wayland are checked by GLFW's own CMake when it configures

	# Find OpenGL (system-provided, always required)
	find_package(OpenGL QUIET)
	if(NOT OpenGL_FOUND)
		message(STATUS "OpenGL not found")
		set(GUI_DEPENDENCIES_AVAILABLE FALSE PARENT_SCOPE)
	else()
		message(STATUS "OpenGL found")
		set(GUI_DEPENDENCIES_AVAILABLE TRUE PARENT_SCOPE)
	endif()
endfunction()

# Check for GUI dependencies
check_gui_dependencies()

# Print configuration summary
message(STATUS "=== Build Configuration ===")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "GUI dependencies available: ${GUI_DEPENDENCIES_AVAILABLE}")
message(STATUS "Force GUI build: ${FORCE_GUI_BUILD}")

# Decide what to build - GUI only
if(NOT GUI_DEPENDENCIES_AVAILABLE)
	message(FATAL_ERROR "GUI dependencies are missing! Install required packages first.")
endif()

# Link threading library
find_package(Threads REQUIRED)

message(STATUS "Building GUI version with ImGui interface")

set(RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/upstream")

# Platform-specific resource files
set(RESOURCE_FILES "")
if(PLATFORM_WINDOWS)
	if(EXISTS "${RESOURCES_DIR}/app.rc")
		set(RESOURCE_FILES ${RESOURCES_DIR}/app.rc)
		message(STATUS "Windows icon resource found: app.rc")
	endif()
endif()

add_subdirectory(code)


# === Installation ===
install(TARGETS Compilatron
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Fonts — namespaced under compilatron-data/ to avoid collisions in shared bin dirs
install(FILES
	"${CMAKE_SOURCE_DIR}/external/imgui/upstream/misc/fonts/Roboto-Medium.ttf"
	"${CMAKE_SOURCE_DIR}/external/fonts/fa-solid-900.ttf"
	DESTINATION "${CMAKE_INSTALL_BINDIR}/compilatron-data/fonts")

if(PLATFORM_LINUX)
	# PNG icons — multiple sizes for desktop environment compatibility
	foreach(_size 32 48 128 256)
		if(_size EQUAL 32)
			set(_icon_file "${RESOURCES_DIR}/app_icon.png")
		else()
			set(_icon_file "${RESOURCES_DIR}/app_icon_${_size}.png")
		endif()
		if(EXISTS "${_icon_file}")
			install(FILES "${_icon_file}"
				DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${_size}x${_size}/apps"
				RENAME "compilatron.png")
		endif()
	endforeach()

	# Desktop file + uninstall script — generated at install time so they embed the real prefix.
	# configure_file() would bake in the configure-time prefix, which differs when the user
	# passes CMAKE_INSTALL_PREFIX only to cmake --install (e.g. make install PREFIX=~/.local).
	install(CODE "
		set(_bindir  \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
		set(_datadir \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATAROOTDIR}\")
		set(CMAKE_INSTALL_FULL_BINDIR  \"\${_bindir}\")
		set(CMAKE_INSTALL_FULL_DATAROOTDIR \"\${_datadir}\")

		# Desktop file — absolute Exec= paths so desktop actions work regardless of session PATH
		file(READ \"${RESOURCES_DIR}/compilatron.desktop.in\" _d)
		file(CONFIGURE OUTPUT \"\${_datadir}/applications/compilatron.desktop\"
			CONTENT \"\${_d}\" @ONLY)
		message(STATUS \"Installing: \${_datadir}/applications/compilatron.desktop\")

		# Uninstall script — must know the real prefix to remove the right files
		file(READ \"${RESOURCES_DIR}/compilatron-uninstall.sh.in\" _t)
		file(CONFIGURE OUTPUT \"\${_bindir}/compilatron-uninstall\"
			CONTENT \"\${_t}\" @ONLY)
		file(CHMOD \"\${_bindir}/compilatron-uninstall\" FILE_PERMISSIONS
			OWNER_READ OWNER_WRITE OWNER_EXECUTE
			GROUP_READ GROUP_EXECUTE
			WORLD_READ WORLD_EXECUTE)
		message(STATUS \"Installing: \${_bindir}/compilatron-uninstall\")
	")

	# Post-install: refresh desktop and icon caches (silent — tools may not be present)
	install(CODE "
		execute_process(
			COMMAND update-desktop-database -q
				\"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATAROOTDIR}/applications\"
			ERROR_QUIET OUTPUT_QUIET)
		execute_process(
			COMMAND gtk-update-icon-cache -q -t -f
				\"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor\"
			ERROR_QUIET OUTPUT_QUIET)
	")
endif()

# Uninstall target — removes files recorded in install_manifest.txt
add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND}
        -DCMAKE_CURRENT_BINARY_DIR=${CMAKE_BINARY_DIR}
        -P "${CMAKE_SOURCE_DIR}/cmake/uninstall.cmake"
    COMMENT "Uninstalling Compilatron"
)

# Custom clean target to remove additional files
add_custom_target(clean-all
	COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
	COMMAND ${CMAKE_COMMAND} -E remove -f debug.log imgui.ini
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	COMMENT "Cleaning build artifacts and runtime files"
)

# === Build summary ===
message(STATUS "=== Build Summary ===")
message(STATUS "✅ GUI version will be built")
message(STATUS "🎯 Primary executable: Compilatron")
message(STATUS "========================")

# === Dependency installation help ===
if(NOT GUI_DEPENDENCIES_AVAILABLE)
	message(STATUS "")
	message(STATUS "📦 To install GUI dependencies:")
	if(PLATFORM_LINUX)
		message(STATUS "   Ubuntu/Debian: sudo apt install cmake build-essential ninja-build libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev")
		message(STATUS "   Fedora:        sudo dnf install cmake gcc gcc-c++ ninja-build mesa-libGL-devel libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel")
		message(STATUS "   Arch:          sudo pacman -S cmake base-devel ninja mesa libx11 libxrandr libxinerama libxcursor libxi")
	elseif(PLATFORM_WINDOWS)
		message(STATUS "   Windows:       Install vcpkg and run: vcpkg install glfw3 opengl")
	endif()
	message(STATUS "")
endif()