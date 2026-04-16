set(manifest "${CMAKE_CURRENT_BINARY_DIR}/install_manifest.txt")

if(NOT EXISTS "${manifest}")
    message(FATAL_ERROR "Cannot uninstall: ${manifest} not found.\n"
        "Run 'cmake --install' first to generate the install manifest.")
endif()

file(READ "${manifest}" files)
string(REGEX REPLACE "\n" ";" files "${files}")

foreach(file ${files})
    if(EXISTS "${file}")
        message(STATUS "Removing: ${file}")
        file(REMOVE "${file}")
    else()
        message(STATUS "Already removed: ${file}")
    endif()
endforeach()

message(STATUS "Uninstall complete.")
