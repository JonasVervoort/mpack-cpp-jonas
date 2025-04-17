project(test-mpack)
cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 17)

# Include mpack headers
include_directories(mpack/src include)

# Add source files
set(MPACK_SOURCES
    mpack/src/mpack/mpack-common.c
    mpack/src/mpack/mpack-expect.c
    mpack/src/mpack/mpack-node.c
    mpack/src/mpack/mpack-platform.c
    mpack/src/mpack/mpack-reader.c
    mpack/src/mpack/mpack-writer.c
)

# Add the C file to be compiled
add_executable(mpack_example
    main.cpp
    ${MPACK_SOURCES}
)

# Tell the compiler it's a C file
foreach(source_file ${MPACK_SOURCES})
  set_source_files_properties(${source_file} PROPERTIES LANGUAGE C)
endforeach()
# Generate a compile_commands.json file for editor IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)