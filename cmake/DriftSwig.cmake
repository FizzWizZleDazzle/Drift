# DriftSwig.cmake - SWIG C# binding generation for Drift engine
#
# Usage: include after defining the drift target.
# Set DRIFT_BUILD_CSHARP=ON to enable.

option(DRIFT_BUILD_CSHARP "Build C# bindings via SWIG" OFF)

if(NOT DRIFT_BUILD_CSHARP)
    return()
endif()

find_package(SWIG 4.1 REQUIRED)
include(${SWIG_USE_FILE})

set(SWIG_INTERFACE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/bindings/swig/drift.i)
set(CSHARP_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bindings/csharp/Drift.Native/Generated)

# Ensure output directory exists
file(MAKE_DIRECTORY ${CSHARP_OUTPUT_DIR})

set(CMAKE_SWIG_OUTDIR ${CSHARP_OUTPUT_DIR})
set(SWIG_OUTFILE_DIR ${CMAKE_BINARY_DIR}/swig)

set_source_files_properties(${SWIG_INTERFACE_FILE} PROPERTIES
    CPLUSPLUS ON
    SWIG_FLAGS "-namespace;drift;-I${CMAKE_CURRENT_SOURCE_DIR}/include"
)

swig_add_library(drift_csharp
    TYPE SHARED
    LANGUAGE csharp
    SOURCES ${SWIG_INTERFACE_FILE}
)

target_include_directories(drift_csharp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(drift_csharp PRIVATE drift)

set_target_properties(drift_csharp PROPERTIES
    OUTPUT_NAME "drift_csharp"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# Post-build: inform user how to build C# examples
add_custom_command(TARGET drift_csharp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "SWIG C# bindings generated in ${CSHARP_OUTPUT_DIR}"
    COMMENT "C# bindings ready"
)
