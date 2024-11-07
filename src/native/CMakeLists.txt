cmake_minimum_required(VERSION 3.10)
project(xiso_extractor C)

# Enable large file support
add_definitions(-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE)

# Set compiler flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O2")
endif()

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Add library
add_library(xiso SHARED
    src/xiso.c
)

# Add test executable
add_executable(test_xiso
    src/test_xiso.c
)

# Link test executable with library
target_link_libraries(test_xiso xiso)

# Set library properties
if(WIN32)
    set_target_properties(xiso PROPERTIES 
        PREFIX ""
        OUTPUT_NAME "xiso")
else()
    set_target_properties(xiso PROPERTIES 
        PREFIX "lib"
        OUTPUT_NAME "xiso")
endif()

# Enable position independent code
set_property(TARGET xiso PROPERTY POSITION_INDEPENDENT_CODE ON)
