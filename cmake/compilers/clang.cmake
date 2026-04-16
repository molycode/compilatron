add_compile_options(
	-g                      # Debug symbols
	-Wall                   # Enable most warnings
	-Wextra                 # Enable extra warnings
	-Wpedantic              # Pedantic standard conformance
	-Werror                 # Treat warnings as errors
	-Wno-unused-parameter   # Suppress unused parameter warnings
	-fno-exceptions         # Disable C++ exceptions
)

add_compile_options(
	$<$<COMPILE_LANGUAGE:CXX>:-stdlib=libstdc++>    # Use libstdc++ (GCC standard library)
)

message(STATUS "Clang ${CMAKE_CXX_COMPILER_VERSION} compiler flags configured")
