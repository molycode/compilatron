add_compile_options(
	-g                      # Debug symbols
	-Wall                   # Enable most warnings
	-Wextra                 # Enable extra warnings
	-Wpedantic              # Pedantic standard conformance
	-Werror                 # Treat warnings as errors
	-Wno-unused-parameter   # Suppress unused parameter warnings
	-fno-exceptions         # Disable C++ exceptions
)

message(STATUS "GCC ${CMAKE_CXX_COMPILER_VERSION} compiler flags configured")
